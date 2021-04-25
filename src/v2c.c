#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <archive.h>
#include <archive_entry.h>
#include <convirter/oci/blob.h>
#include <convirter/oci/config.h>
#include <convirter/oci/image.h>
#include <convirter/oci/manifest.h>
#include <convirter/oci/layer.h>
#include <guestfs.h>

static const char usage[] = "Usage: v2c INPUT OUTPUT";

static const int bufsz = 3 * 1024 * 1024;

struct archive_entry *entry = NULL;

struct archive_entry_linkresolver *resolver = NULL;

static int dump_file_content(guestfs_h *g, const char *path, struct archive *archive, int64_t size) {
	// guestfs_read_file does guestfs_download and reads the whole file into memory
	// read by our own to control buffer size
	// guestfs_download have it's own (allocated and freed at each batch) buffer and extra IO
	// guestfs_pread, although still allocates buffer at each call, is the fatest one
	int64_t offset = 0;
	size_t read = 0;
	int res = 0;
	while (size) {
		char *buf = guestfs_pread(g, path, size > bufsz ? bufsz : size, offset, &read);
		if (!buf) {
			return -errno;
		}
		size -= read;
		offset += read;
		res = archive_write_data(archive, buf, read);
		if (res < 0) {
			return -errno;
		}
		free(buf);
	}
	return 0;
}

static int dump_to_archive(guestfs_h *g, const char *path,
		struct guestfs_statns *stat, struct archive *archive) {
	if (!stat) {
		return -errno;
	}
	archive_entry_clear(entry);
	archive_entry_set_pathname(entry, &path[1]);
	struct stat tmpstat = {
		.st_dev = stat->st_dev,
		.st_ino = stat->st_ino,
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
	if ((stat->st_mode & S_IFMT) == S_IFLNK) {
		char *link = guestfs_readlink(g, path);
		if (!link) {
			return -errno;
		}
		archive_entry_set_symlink(entry, link);
		free(link);
	}

	struct guestfs_xattr_list *xattrs = guestfs_lgetxattrs(g, path);
	for (int i = 0; i < xattrs->len; i++) {
		archive_entry_xattr_add_entry(entry, xattrs->val[i].attrname,
			xattrs->val[i].attrval, xattrs->val[i].attrval_len);
	}
	guestfs_free_xattr_list(xattrs);

	// in pax, data is stored on first seen, and archive_entry_linkify
	// *sparse is for format where data is stored at last seen.
	struct archive_entry *dummy;
	archive_entry_linkify(resolver, &entry, &dummy);
	archive_write_header(archive, entry);

	// size is set to zero if linkify found hardlink to previous entry
	if ((stat->st_mode & S_IFMT) == S_IFREG && archive_entry_size(entry)) {
		dump_file_content(g, path, archive, stat->st_size);
	}
	return 0;
}

static int dump_guestfs(guestfs_h *g, const char *dir, struct archive *archive) {
	char **ls = guestfs_ls(g, dir);
	if (!ls) {
		return -errno;
	} else if (!ls[0]) {
		return 0;
	}
	struct guestfs_statns_list *stats = guestfs_lstatnslist(g, dir, ls);
	if (!stats) {
		return -errno;
	}

	for (int i = 0; ls[i]; i++) {
		if (!strncmp(ls[i], ".wh.", 4)) {
			continue;
		}
		char *full;
		if (dir[1]) {
			full = calloc(
				strlen(dir) + strlen(ls[i]) + 2,
				sizeof(char));
			if (!full) {
				guestfs_free_statns_list(stats);
				return -errno;
			}
			strcpy(full, dir);
			strcat(full, "/");
			strcat(full, ls[i]);
		} else {
			full = calloc(strlen(ls[i]) + 2, sizeof(char));
			if (!full) {
				guestfs_free_statns_list(stats);
				return -errno;
			}
			strcpy(full, "/");
			strcat(full, ls[i]);
		}
		//printf("%s\n", full);
		int res = dump_to_archive(g, full, &stats->val[i], archive);
		if (res) {
			guestfs_free_statns_list(stats);
			return res;
		}
		if ((stats->val[i].st_mode & S_IFMT) == S_IFDIR) {
			res = dump_guestfs(g, full, archive);
			free(full);
			if (res) {
				guestfs_free_statns_list(stats);
				return res;
			}
		}
		free(ls[i]);
	}
	guestfs_free_statns_list(stats);
	free(ls);
	return 0;
}

static int mounts_cmp(const void *a, const void *b) {
	return strcmp(*(const char **)a, *(const char **)b);
}

int main(int argc, char *argv[]) {
	int res = 0;
	if (argc != 3) {
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

	resolver = archive_entry_linkresolver_new();
	if (!resolver) {
		perror("archive_entry_linkresolver_new");
		exit(EXIT_FAILURE);
	}
	struct archive *ar = cvirt_oci_layer_get_libarchive(layer);
	archive_entry_linkresolver_set_strategy(resolver, archive_format(ar));

	char **mounts = guestfs_inspect_get_mountpoints(g, roots[0]);
	if (!mounts || !mounts[0]) {
		exit(EXIT_FAILURE);
	}
	int sz = 0;
	while (mounts[sz++]);
	sz /= 2;
	qsort(mounts, sz, sizeof(char *) * 2, mounts_cmp);
	int index = 0;
	while (mounts[index]) {
		if (mounts[index][1] != '\0' && !guestfs_is_dir(g, mounts[index])) {
			res = guestfs_mkdir_p(g, mounts[index]);
			if (res < 0) {
				fprintf(stderr, "Warning: mountpoint %s setup failed.\n",
					mounts[index]);
			}
		}
		res = guestfs_mount(g, mounts[index + 1], mounts[index]);
		if (res < 0) {
			fprintf(stderr, "Warning: mount %s at %s failed.\n",
				mounts[index + 1], mounts[index]);
		}
		index += 2;
	}

	// guestfs_tar_out would let whiteouts fall in
	dump_guestfs(g, "/", ar);

	archive_entry_free(entry);
	archive_entry_linkresolver_free(resolver);
	cvirt_oci_layer_close(layer);

	struct cvirt_oci_config *config = cvirt_oci_config_new();
	cvirt_oci_config_add_layer(config, layer);
	cvirt_oci_config_close(config);

	struct cvirt_oci_blob *config_blob = cvirt_oci_blob_from_config(config);
	struct cvirt_oci_blob *layer_blob = cvirt_oci_blob_from_layer(layer);
	struct cvirt_oci_manifest *manifest = cvirt_oci_manifest_new();
	cvirt_oci_manifest_set_config(manifest, config_blob);
	cvirt_oci_manifest_add_layer(manifest, layer_blob);
	cvirt_oci_manifest_close(manifest);

	struct cvirt_oci_blob *manifest_blob = cvirt_oci_blob_from_manifest(manifest);
	struct cvirt_oci_image *image = cvirt_oci_image_new(argv[2]);
	cvirt_oci_image_add_blob(image, layer_blob);
	cvirt_oci_image_add_blob(image, config_blob);
	cvirt_oci_image_add_manifest(image, manifest_blob);
	cvirt_oci_image_close(image);

	cvirt_oci_image_destroy(image);
	cvirt_oci_blob_destory(manifest_blob);
	cvirt_oci_manifest_destroy(manifest);
	cvirt_oci_blob_destory(layer_blob);
	cvirt_oci_blob_destory(config_blob);
	cvirt_oci_config_destroy(config);
	cvirt_oci_layer_destroy(layer);
	guestfs_umount_all(g);
	guestfs_shutdown(g);
	guestfs_close(g);

	return 0;
}
