#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <archive.h>
#include <archive_entry.h>
#include <convirter/oci/layer.h>
#include <guestfs.h>

#define DOWNLOAD_FILE_TEMPLATE	"/convirter-download-XXXXXX"

static const char usage[] = "Usage: v2c INPUT";

static char buf[1 * 1024 * 1024] = {0};

struct archive_entry *entry = NULL;

static void randname(char *buf) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	long r = ts.tv_nsec;
	for (int i = 0; i < 6; ++i) {
		buf[i] = 'A' + (r & 15) + (r & 16) * 2;
		r >>= 5;
	}
}

static int dump_file_content(guestfs_h *g, const char *path, struct archive *archive, int64_t size) {
	// guestfs_read_file does guestfs_download and reads the whole file into memory
	// read by our own to control buffer size
	const char *tmpdir = getenv("TMPDIR");
	char *template = NULL;
	if (tmpdir) {
		template = calloc(strlen(tmpdir) + strlen(DOWNLOAD_FILE_TEMPLATE) + 1, sizeof(char));
		if (!template) {
			return -errno;
		}
		strcpy(template, tmpdir);
		strcat(template, DOWNLOAD_FILE_TEMPLATE);
	} else {
		template = strdup("/tmp" DOWNLOAD_FILE_TEMPLATE);
		if (!template) {
			return -errno;
		}
	}
	randname(template + strlen(template) - 6);
	int res = guestfs_download(g, path, template);
	if (res < 0) {
		free(template);
		return -errno;
	}
	int fd = open(template, O_RDONLY);
	if (res < 0) {
		free(template);
		return -errno;
	}
	res = read(fd, buf, sizeof(buf));
	if (res < 0) {
		free(template);
		close(fd);
		return -errno;
	}
	size -= res;
	res = archive_write_data(archive, buf, res);
	if (res < 0) {
		free(template);
		close(fd);
		return -errno;
	}
	while (size) {
		res = read(fd, buf, sizeof(buf));
		if (res < 0) {
			free(template);
			close(fd);
			return -errno;
		}
		size -= res;
		res = archive_write_data(archive, buf, res);
		if (res < 0) {
			free(template);
			close(fd);
			return -errno;
		}
	}
	unlink(template);
	close(fd);
	free(template);
	return 0;
}

static int dump_to_archive(guestfs_h *g, const char *path, char type, struct archive *archive) {
	struct guestfs_statns *stat = guestfs_lstatns(g, path);
	if (!stat) {
		return -errno;
	}
	archive_entry_clear(entry);
	archive_entry_set_pathname(entry, &path[1]);
	struct stat tmpstat = {
		.st_dev = stat->st_dev,
		.st_mode = stat->st_mode,
		.st_uid = stat->st_uid,
		.st_gid = stat->st_gid,
		.st_rdev = stat->st_rdev,
		.st_size = stat->st_size,
		.st_atim = {
			.tv_sec = stat->st_atime_sec,
			.tv_nsec = stat->st_atime_nsec,
		},
		.st_mtim = {
			.tv_sec = stat->st_mtime_sec,
			.tv_nsec = stat->st_mtime_nsec,
		},
		.st_ctim = {
			.tv_sec = stat->st_ctime_sec,
			.tv_nsec = stat->st_ctime_nsec,
		},
	};
	archive_entry_copy_stat(entry, &tmpstat);
	if (type == 'l') {
		char *link = guestfs_readlink(g, path);
		if (!link) {
			guestfs_free_statns(stat);
			return -errno;
		}
		archive_entry_set_symlink(entry, link);
		free(link);
	}
	//TODO: ACL
	archive_write_header(archive, entry);
	if (type == 'r') {
		dump_file_content(g, path, archive, stat->st_size);
	}
	guestfs_free_statns(stat);
	return 0;
}

static int dump_guestfs(guestfs_h *g, const char *dir, struct archive *archive) {
	struct guestfs_dirent_list *dirents = guestfs_readdir(g, dir);
	if (!dirents) {
		return -errno;
	}

	for (int i = 0; i < dirents->len; i++) {
		if (!strcmp(dirents->val[i].name, ".") ||
				!strcmp(dirents->val[i].name, "..") ||
				!strncmp(dirents->val[i].name, ".wh.", 4)) {
			continue;
		}
		char *full;
		if (dir[1]) {
			full = calloc(
				strlen(dir) + strlen(dirents->val[i].name) + 2,
				sizeof(char));
			if (!full) {
				guestfs_free_dirent_list(dirents);
				return -errno;
			}
			strcpy(full, dir);
			strcat(full, "/");
			strcat(full, dirents->val[i].name);
		} else {
			full = calloc(strlen(dirents->val[i].name) + 2, sizeof(char));
			if (!full) {
				guestfs_free_dirent_list(dirents);
				return -errno;
			}
			strcpy(full, "/");
			strcat(full, dirents->val[i].name);
		}
		printf("%c %s\n", dirents->val[i].ftyp, full);
		int res = dump_to_archive(g, full, dirents->val[i].ftyp, archive);
		if (res) {
			guestfs_free_dirent_list(dirents);
			return res;
		}
		if (dirents->val[i].ftyp == 'd') {
			res = dump_guestfs(g, full, archive);
			free(full);
			if (res) {
				guestfs_free_dirent_list(dirents);
				return res;
			}
		}
	}
	guestfs_free_dirent_list(dirents);
	return 0;
}

int main(int argc, char *argv[]) {
	int res = 0;
	if (argc != 2) {
		puts(usage);
		exit(EXIT_FAILURE);
	}

	guestfs_h *g = guestfs_create();
	if (!g) {
		exit(EXIT_FAILURE);
	}
	res = guestfs_add_drive_opts(g, argv[1], GUESTFS_ADD_DRIVE_OPTS_READONLY, 1, -1);
	if (res < 0) {
		exit(EXIT_FAILURE);
	}
	res = guestfs_launch(g);
	if (res < 0) {
		exit(EXIT_FAILURE);
	}

	char **roots = guestfs_inspect_os(g);
	if (!roots) {
		exit(EXIT_FAILURE);
	} else if (!roots[0]) {
		fprintf(stderr, "No root found.\n");
		exit(EXIT_FAILURE);
	} else if (roots[1]) {
		fprintf(stderr, "Warning: Multiple roots detected, dumping only first.\n");
	}

	struct cvirt_oci_layer *layer = cvirt_oci_layer_new();
	if (!layer) {
		perror("cvirt_oci_layer_new");
		exit(EXIT_FAILURE);
	}

	entry = archive_entry_new();
	if (!entry) {
		perror("archive_entry_new");
		exit(EXIT_FAILURE);
	}

	char **mounts = guestfs_inspect_get_mountpoints(g, roots[0]);
	if (!mounts || !mounts[0]) {
		exit(EXIT_FAILURE);
	}
	int index = 0;
	while (mounts[index]) {
		res = guestfs_mount_ro(g, mounts[index + 1], mounts[index]);
		if (res < 0) {
			fprintf(stderr, "Warning: %s at %s mount failed.\n",
				mounts[index], mounts[index + 1]);
		}
		index += 2;
	}

	// guestfs_tar_out would let whiteouts fall in
	dump_guestfs(g, "/", cvirt_oci_layer_get_libarchive(layer));

	archive_entry_free(entry);
	cvirt_oci_layer_close(layer);
	cvirt_oci_layer_free(layer);
	guestfs_umount_all(g);
	guestfs_shutdown(g);
	guestfs_close(g);

	return 0;
}
