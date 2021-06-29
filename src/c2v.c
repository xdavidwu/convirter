#include <archive.h>
#include <archive_entry.h>
#include <convirter/oci-r/index.h>
#include <convirter/oci-r/layer.h>
#include <convirter/oci-r/manifest.h>
#include <guestfs.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysmacros.h>

static const int bufsz = 3 * 1024 * 1024;

static int set_attr(guestfs_h *guestfs, const char *path,
		struct archive_entry *entry, const struct stat *stat) {
	int res = guestfs_lchown(guestfs, stat->st_uid, stat->st_gid, path);
	if (res < 0) {
		return res;
	}
	const char *name;
	const void *val;
	size_t sz;
	while (true) {
		archive_entry_xattr_next(entry, &name, &val, &sz);
		if (!name) {
			break;
		}
		guestfs_lsetxattr(guestfs, name, val, sz, path);
	}
	return guestfs_utimens(guestfs, path, stat->st_atim.tv_sec,
		stat->st_atim.tv_nsec, stat->st_mtim.tv_sec, stat->st_mtim.tv_nsec);
}

static int dump_file(guestfs_h *guestfs, struct archive *archive,
		const char *path, const struct stat *stat) {
	int res = guestfs_truncate_size(guestfs, path, stat->st_size);
	if (res < 0) {
		return res;
	}
	const char *cbuf;
	const void *buf;
	size_t size;
	off_t offset;
	while (archive_read_data_block(archive, &buf, &size, &offset) != ARCHIVE_EOF) {
		cbuf = buf;
		int buf_offset = 0;
		while (size > bufsz) {
			res = guestfs_pwrite(guestfs, path, &cbuf[buf_offset], bufsz, offset);
			if (res < 0) {
				return res;
			}
			offset += bufsz;
			buf_offset += bufsz;
			size -= bufsz;
		}
		res = guestfs_pwrite(guestfs, path, &cbuf[buf_offset], size, offset);
		if (res < 0) {
			return res;
		}
	}
	return res;
}

static int dump_layer(guestfs_h *guestfs, struct archive *archive) {
	// TODO: whiteouts
	struct archive_entry *entry;
	int res = 0;
	while (archive_read_next_header(archive, &entry) == ARCHIVE_OK) {
		const struct stat *stat = archive_entry_stat(entry);
		const char *path = archive_entry_pathname(entry);
		char *abs_path = calloc(2 + strlen(path), sizeof(char));
		const char *link;
		abs_path[0] = '/';
		strcat(abs_path, path);
		guestfs_rm_rf(guestfs, abs_path);
		switch (stat->st_mode & S_IFMT) {
		case S_IFLNK:
			res = guestfs_ln_s(guestfs, archive_entry_symlink(entry), abs_path);
			break;
		case S_IFREG:
			res = guestfs_mknod(guestfs, stat->st_mode,
				major(stat->st_rdev), minor(stat->st_rdev),
				abs_path);
			if (res < 0) {
				break;
			}
			res = dump_file(guestfs, archive, abs_path, stat);
			break;
		case S_IFDIR:
			res = guestfs_mkdir_mode(guestfs, abs_path, stat->st_mode);
			break;
		case S_IFSOCK:
		case S_IFBLK:
		case S_IFCHR:
		case S_IFIFO:
			res = guestfs_mknod(guestfs, stat->st_mode,
				major(stat->st_rdev), minor(stat->st_rdev),
				abs_path);
			break;
		default:
			// hardlinks?
			link = archive_entry_hardlink(entry);
			if (link) {
				char *abs_target = calloc(2 + strlen(link),
					sizeof(char));
				abs_target[0] = '/';
				strcat(abs_target, link);
				res = guestfs_ln(guestfs, abs_target, abs_path);
				free(abs_target);
			} else {
				fprintf(stderr, "dump_layer: unrecognized file type at %s\n", path);
			}
			continue;
		}
		if (res < 0) {
			return res;
		}
		res = set_attr(guestfs, abs_path, entry, stat);
		if (res < 0) {
			return res;
		}
		free(abs_path);
	}
	return 0;
}

int main(int argc, char *argv[]) {
	if (argc != 3) {
		exit(EXIT_FAILURE);
	}
	guestfs_h *guestfs = guestfs_create();
	if (!guestfs) {
		exit(EXIT_FAILURE);
	}
	int res = guestfs_disk_create(guestfs, argv[2], "qcow2", 4LL * 1024 * 1024 * 1024, -1);
	if (res < 0) {
		fprintf(stderr, "Failed creating new disk\n");
		exit(EXIT_FAILURE);
	}
	res = guestfs_add_drive_opts(guestfs, argv[2], GUESTFS_ADD_DRIVE_OPTS_FORMAT, "qcow2", -1);
	if (res < 0) {
		fprintf(stderr, "Failed adding new disk\n");
		exit(EXIT_FAILURE);
	}
	res = guestfs_launch(guestfs);
	if (res < 0) {
		fprintf(stderr, "Failed launching guestfs\n");
		exit(EXIT_FAILURE);
	}
	// TODO: check avail
	char *btrfs_devices[] = {
		"/dev/sda",
		NULL,
	};
	res = guestfs_mkfs_btrfs(guestfs, btrfs_devices, -1);
	if (res < 0) {
		fprintf(stderr, "Failed mkfs\n");
		exit(EXIT_FAILURE);
	}
	res = guestfs_mount(guestfs, "/dev/sda", "/");
	if (res < 0) {
		fprintf(stderr, "Failed mount\n");
		exit(EXIT_FAILURE);
	}
	guestfs_umask(guestfs, 0);

	struct cvirt_oci_r_index *index = cvirt_oci_r_index_from_archive(argv[1]);
	const char *manifest_digest = cvirt_oci_r_index_get_native_manifest_digest(index);
	struct cvirt_oci_r_manifest *manifest = cvirt_oci_r_manifest_from_archive_blob(argv[1], manifest_digest);
	const char *config_digest = cvirt_oci_r_manifest_get_config_digest(manifest);
	int len = cvirt_oci_r_manifest_get_layers_length(manifest);
	for (int i = 0; i < len; i++) {
		const char *layer_digest = cvirt_oci_r_manifest_get_layer_digest(manifest, i);
		struct cvirt_oci_r_layer *layer =
			cvirt_oci_r_layer_from_archive_blob(argv[1], layer_digest,
			cvirt_oci_r_manifest_get_layer_compression(manifest, i));
		struct archive *archive = cvirt_oci_r_layer_get_libarchive(layer);
		dump_layer(guestfs, archive);
		cvirt_oci_r_layer_destroy(layer);
	}
	cvirt_oci_r_manifest_destroy(manifest);
	cvirt_oci_r_index_destroy(index);

	guestfs_umount_all(guestfs);
	guestfs_shutdown(guestfs);
	guestfs_close(guestfs);
	return 0;
}
