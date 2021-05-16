#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include <archive.h>
#include <archive_entry.h>
#include <convirter/oci/blob.h>
#include <convirter/oci/config.h>
#include <convirter/oci/image.h>
#include <convirter/oci/manifest.h>
#include <convirter/oci/layer.h>
#include <guestfs.h>

static const char usage[] = "Usage: v2c INPUT OUTPUT";

// TODO: we may want this individually configurable
static const char *temporary_paths[] = {
	// needs trailing slash
	"/tmp/",
	"/run/",
	"/var/tmp/",
	"/var/cache/",
	NULL,
};

static const int bufsz = 3 * 1024 * 1024;

static struct archive_entry *entry = NULL;

static struct archive_entry_linkresolver *resolver = NULL;

static bool set_modification_epoch = false;

static time_t modification_start, modification_end, source_date_epoch;

static int dump_file_content(guestfs_h *g, const char *path, struct archive *archive, int64_t size) {
	// guestfs_read_file does guestfs_download and reads the whole file into memory
	// read by our own to control buffer size
	// guestfs_download have it's own (allocated and freed at each batch) buffer and extra IO
	// guestfs_pread, although still allocates buffer at each call, is the fastest one
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
		free(buf);
		if (res < 0) {
			return -errno;
		}
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

	if (set_modification_epoch) {
		if (tmpstat.st_atim.tv_sec >= modification_start &&
				tmpstat.st_atim.tv_sec <= modification_end) {
			tmpstat.st_atim.tv_sec = source_date_epoch;
			tmpstat.st_atim.tv_nsec = 0;
		}
		if (tmpstat.st_mtim.tv_sec >= modification_start &&
				tmpstat.st_mtim.tv_sec <= modification_end) {
			tmpstat.st_mtim.tv_sec = source_date_epoch;
			tmpstat.st_mtim.tv_nsec = 0;
		}
		if (tmpstat.st_ctim.tv_sec >= modification_start &&
				tmpstat.st_ctim.tv_sec <= modification_end) {
			tmpstat.st_ctim.tv_sec = source_date_epoch;
			tmpstat.st_ctim.tv_nsec = 0;
		}
	}

	archive_entry_copy_stat(entry, &tmpstat);
	if ((stat->st_mode & S_IFMT) == S_IFLNK) {
		char *link = guestfs_readlink(g, path);
		if (!link) {
			free(link);
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
	for (int i = 0; temporary_paths[i]; i++) {
		if (!strcmp(dir, temporary_paths[i])) {
			return 0;
		}
	}

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

	int res = 0;
	for (int i = 0; ls[i]; i++) {
		if (!strncmp(ls[i], ".wh.", 4)) {
			// no way to store names like whiteouts in OCI
			continue;
		}

		char *full;
		if (dir[1]) {
			full = calloc(strlen(dir) + strlen(ls[i]) + 2, sizeof(char));
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
		free(ls[i]);

		res = dump_to_archive(g, full, &stats->val[i], archive);
		if (res < 0) {
			free(full);
			goto cleanup;
		}

		if ((stats->val[i].st_mode & S_IFMT) == S_IFDIR) {
			res = dump_guestfs(g, full, archive);
			if (res < 0) {
				free(full);
				goto cleanup;
			}
		}
		free(full);
	}
cleanup:
	guestfs_free_statns_list(stats);
	free(ls);
	return res;
}

static int mounts_cmp(const void *a, const void *b) {
	return strcmp(*(const char **)a, *(const char **)b);
}

static int cleanup_fstab(guestfs_h *g, char **mounts) {
	/* TODO: make it optional */
	guestfs_aug_init(g, "/", 0);
	int index = 0, res = 0;
	while (mounts[index]) {
		char exp[27 + strlen(mounts[index]) + 1];
		// TODO: also match devices?
		// needs to consider: UUID, PARTUUID, LABEL, PARTLABEL
		sprintf(exp, "/files/etc/fstab/*[file='%s']", mounts[index]);
		res = guestfs_aug_rm(g, exp);
		if (res < 0) {
			fprintf(stderr, "Error: fstab cleanup mount %s at %s failed.\n",
				mounts[index + 1], mounts[index]);
			return res;
		}
		index += 2;
	}
	res = guestfs_aug_save(g);
	if (res < 0) {
		fprintf(stderr, "Error: saving edited fstab failed.\n");
		return res;
	}
	return guestfs_aug_close(g);
}

static int cleanup_systemd(guestfs_h *g) {
	/* TODO: organize by category (e.g. network) and make them optional */
	// Debian ifup
	{
		char *const cmd[] = {
			"systemctl",
			"disable",
			"networking.service",
			NULL,
		};
		free(guestfs_command(g, cmd));
	}
	// Ubuntu multipathd
	{
		char *const cmd[] = {
			"systemctl",
			"disable",
			"multipathd.service",
			NULL,
		};
		free(guestfs_command(g, cmd));
	}
	// Ubuntu systemd-rfkill (socket)
	{
		char *const cmd[] = {
			"systemctl",
			"mask",
			"systemd-rfkill.socket",
			NULL,
		};
		free(guestfs_command(g, cmd));
	}
	// CentOS auditd
	// Only one instance should run, leave it to host
	// (https://access.redhat.com/articles/4494341)
	{
		char *const cmd[] = {
			"systemctl",
			"disable",
			"auditd.service",
			NULL,
		};
		free(guestfs_command(g, cmd));
	}
	return 0;
}

static int setup_systemd_config(struct cvirt_oci_config *config) {
	int res = cvirt_oci_config_set_user(config, "0:0");
	if (res < 0) {
		return res;
	}
	char *const cmd[] = {
		"/sbin/init",
		NULL,
	};
	res = cvirt_oci_config_set_cmd(config, cmd);
	if (res < 0) {
		return res;
	}
	return cvirt_oci_config_set_stop_signal(config, "SIGRTMIN+3");
}

int main(int argc, char *argv[]) {
	int res = 0;
	if (argc != 3) {
		puts(usage);
		exit(EXIT_FAILURE);
	}

	char *source_date_epoch_env = getenv("SOURCE_DATE_EPOCH");
	if (source_date_epoch_env) {
		set_modification_epoch = true;
		source_date_epoch = atoll(source_date_epoch_env);
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
	char *target_root = NULL;
	if (!roots) {
		exit(EXIT_FAILURE);
	} else if (!roots[0]) {
		fprintf(stderr, "No root found.\n");
		exit(EXIT_FAILURE);
	}
	for (int i = 0; roots[i]; i++) {
		char *type = guestfs_inspect_get_type(g, roots[i]);
		if (!type) {
			free(roots[i]);
			continue;
		}
		if (!strcmp(type, "linux")) {
			target_root = roots[i];
			break;
		}
		free(roots[i]);
	}
	free(roots);
	if (!target_root) {
		fprintf(stderr, "Cannot find any Linux-based OS\n");
		exit(EXIT_FAILURE);
	}

	struct cvirt_oci_layer *layer = cvirt_oci_layer_new(CVIRT_OCI_LAYER_COMPRESSION_ZSTD, 0);
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

	modification_start = time(NULL);

	char **mounts = guestfs_inspect_get_mountpoints(g, target_root);
	free(target_root);
	if (!mounts || !mounts[0]) {
		exit(EXIT_FAILURE);
	}
	int sz = 0;
	while (mounts[sz += 2]);
	sz /= 2;
	qsort(mounts, sz, sizeof(char *) * 2, mounts_cmp);
	for (int index = 0; mounts[index]; index += 2) {
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
	}

	cleanup_fstab(g, mounts);

	for (int i = 0; mounts[i]; i++) {
		free(mounts[i]);
	}
	free(mounts);

	cleanup_systemd(g);

	modification_end = time(NULL);

	// guestfs_tar_out would let whiteouts fall in
	dump_guestfs(g, "/", ar);

	archive_entry_free(entry);
	archive_entry_linkresolver_free(resolver);
	cvirt_oci_layer_close(layer);

	struct cvirt_oci_config *config = cvirt_oci_config_new();
	cvirt_oci_config_add_layer(config, layer);
	setup_systemd_config(config);
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
