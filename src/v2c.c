#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
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

#include "../common/guestfs.h"
#include "../common/common-config.h"
#include "list.h"

static const char usage[] = "\
Usage: %s [OPTION]... INPUT OUTPUT\n\
Convert a VM image into OCI-compatible container image.\n\
\n\
      --compression=ALGO[:LEVEL]  Compress layers with algorithm ALGO\n\
                                  Available algorithms: zstd, gzip, none\n\
                                  Defaults to zstd\n\
      --no-systemd-cleanup        Disable removing systemd units that will\n\
                                  likely fail and is unneeded in containers\n\
\n\
Options below set respective config of output container image:\n"
COMMON_EXEC_CONFIG_OPTIONS_HELP;

static const int common_exec_config_start = 128;

static const struct option long_options[] = {
	{"compression",	required_argument,	NULL,	1},
	{"no-systemd-cleanup",	no_argument,	NULL,	2},
	COMMON_EXEC_CONFIG_LONG_OPTIONS(common_exec_config_start),
	{0},
};

// TODO: we may want this individually configurable
static const char *temporary_paths[] = {
	// needs trailing slash
	"/tmp/",
	"/run/",
	"/var/tmp/",
	"/var/cache/",
	NULL,
};

static const int bufsz = 4000 * 1024;

struct v2c_state {
	guestfs_h *guestfs;
	struct archive *layer_archive;
	struct archive_entry *layer_entry;
	struct archive_entry_linkresolver *layer_link_resolver;
	time_t modification_start, modification_end;
	struct {
		time_t source_date_epoch;
		bool set_modification_epoch;
		enum cvirt_oci_layer_compression compression;
		int compression_level;
		bool disable_systemd_cleanup;
		struct common_exec_config exec;
	} config;
};

static int parse_options(struct v2c_state *state, int argc, char *argv[]) {
	int opt;
	while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
		if (opt >= common_exec_config_start) {
			int res = parse_common_exec_opt(&state->config.exec,
				opt - common_exec_config_start);
			if (res < 0) {
				return res;
			}
			continue;
		}
		switch (opt) {
		case '?':
			return -EINVAL;
		case 1:
			if (!strcmp(optarg, "none")) {
				state->config.compression = CVIRT_OCI_LAYER_COMPRESSION_NONE;
			} else if (!strncmp(optarg, "gzip", 4)) {
				state->config.compression = CVIRT_OCI_LAYER_COMPRESSION_GZIP;
				if (optarg[4] == ':') {
					state->config.compression_level = atoi(&optarg[5]);
				} else if (optarg[4]) {
					return -EINVAL;
				}
			} else if (!strncmp(optarg, "zstd", 4)) {
				state->config.compression = CVIRT_OCI_LAYER_COMPRESSION_ZSTD;
				if (optarg[4] == ':') {
					state->config.compression_level = atoi(&optarg[5]);
				} else if (optarg[4]) {
					return -EINVAL;
				}
			} else {
				return -EINVAL;
			}
			break;
		case 2:
			state->config.disable_systemd_cleanup = true;
			break;
		}
	}
	return 0;
}

static int dump_file_content(struct v2c_state *state, const char *path, int64_t size) {
	// guestfs_read_file does guestfs_download and reads the whole file into memory
	// read by our own to control buffer size
	// guestfs_download have it's own (allocated and freed at each batch) buffer and extra IO
	// guestfs_pread, although still allocates buffer at each call, is the fastest one
	int64_t offset = 0;
	size_t read = 0;
	int res = 0;
	while (offset != size) {
		char *buf = guestfs_pread(state->guestfs, path,
			size > bufsz ? bufsz : size, offset, &read);
		if (!buf) {
			return -errno;
		}
		offset += read;
		res = archive_write_data(state->layer_archive, buf, read);
		free(buf);
		if (res < 0) {
			return -errno;
		}
	}
	return 0;
}

static int dump_to_archive(struct v2c_state *state, const char *path,
		struct guestfs_statns *stat) {
	if (!stat) {
		return -errno;
	}
	archive_entry_clear(state->layer_entry);
	archive_entry_set_pathname(state->layer_entry, &path[1]);
	struct stat tmpstat = {
		.st_dev = stat->st_dev,
		.st_ino = stat->st_ino,
		.st_mode = stat->st_mode,
		.st_uid = stat->st_uid,
		.st_gid = stat->st_gid,
		.st_rdev = stat->st_rdev,
		.st_size = stat->st_size,
		.st_nlink = stat->st_nlink,
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

	if (state->config.set_modification_epoch) {
		if (tmpstat.st_atim.tv_sec >= state->modification_start &&
				tmpstat.st_atim.tv_sec <= state->modification_end) {
			tmpstat.st_atim.tv_sec = state->config.source_date_epoch;
			tmpstat.st_atim.tv_nsec = 0;
		}
		if (tmpstat.st_mtim.tv_sec >= state->modification_start &&
				tmpstat.st_mtim.tv_sec <= state->modification_end) {
			tmpstat.st_mtim.tv_sec = state->config.source_date_epoch;
			tmpstat.st_mtim.tv_nsec = 0;
		}
		if (tmpstat.st_ctim.tv_sec >= state->modification_start &&
				tmpstat.st_ctim.tv_sec <= state->modification_end) {
			tmpstat.st_ctim.tv_sec = state->config.source_date_epoch;
			tmpstat.st_ctim.tv_nsec = 0;
		}
	}

	archive_entry_copy_stat(state->layer_entry, &tmpstat);
	if ((stat->st_mode & S_IFMT) == S_IFLNK) {
		char *link = guestfs_readlink(state->guestfs, path);
		if (!link) {
			return -errno;
		}
		archive_entry_set_symlink(state->layer_entry, link);
		free(link);
	}

	struct guestfs_xattr_list *xattrs = guestfs_lgetxattrs(state->guestfs, path);
	for (int i = 0; i < xattrs->len; i++) {
		archive_entry_xattr_add_entry(state->layer_entry,
			xattrs->val[i].attrname, xattrs->val[i].attrval,
			xattrs->val[i].attrval_len);
	}
	guestfs_free_xattr_list(xattrs);

	// in pax, data is stored on first seen, and archive_entry_linkify
	// *sparse is for format where data is stored at last seen.
	struct archive_entry *dummy;
	archive_entry_linkify(state->layer_link_resolver, &state->layer_entry, &dummy);
	archive_write_header(state->layer_archive, state->layer_entry);

	// size is set to zero if linkify found hardlink to previous entry
	if ((stat->st_mode & S_IFMT) == S_IFREG && archive_entry_size(state->layer_entry)) {
		dump_file_content(state, path, stat->st_size);
	}
	return 0;
}

static int dump_guestfs(struct v2c_state *state, const char *dir) {
	for (int i = 0; temporary_paths[i]; i++) {
		if (!strcmp(dir, temporary_paths[i])) {
			return 0;
		}
	}

	char **ls = guestfs_ls(state->guestfs, dir);
	if (!ls) {
		return -errno;
	} else if (!ls[0]) {
		free(ls);
		return 0;
	}
	struct guestfs_statns_list *stats = guestfs_lstatnslist(state->guestfs, dir, ls);
	if (!stats) {
		return -errno;
	}

	int res = 0, i = 0;
	for (; ls[i]; i++) {
		if (!strncmp(ls[i], ".wh.", 4)) {
			// no way to store names like whiteouts in OCI
			continue;
		}

		char *full;
		if (dir[1]) { // not /
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
			full[0] = '/';
			strcpy(&full[1], ls[i]);
		}
		free(ls[i]);

		res = dump_to_archive(state, full, &stats->val[i]);
		if (res < 0) {
			free(full);
			goto cleanup;
		}

		if ((stats->val[i].st_mode & S_IFMT) == S_IFDIR) {
			res = dump_guestfs(state, full);
			if (res < 0) {
				free(full);
				goto cleanup;
			}
		}
		free(full);
	}
cleanup:
	for (; ls[i]; i++) {
		free(ls[i]);
	}
	guestfs_free_statns_list(stats);
	free(ls);
	return res;
}

static int cleanup_fstab(struct v2c_state *state, char **mounts) {
	/* TODO: make it optional */
	guestfs_aug_init(state->guestfs, "/", 0);
	int res = 0;
	for (int index = 0; mounts[index]; index += 2) {
		char exp[27 + strlen(mounts[index]) + 1];
		// TODO: also match devices?
		// needs to consider: UUID, PARTUUID, LABEL, PARTLABEL
		sprintf(exp, "/files/etc/fstab/*[file='%s']", mounts[index]);
		res = guestfs_aug_rm(state->guestfs, exp);
		if (res < 0) {
			fprintf(stderr, "Error: fstab cleanup mount %s at %s failed.\n",
				mounts[index + 1], mounts[index]);
			goto cleanup;
		}
	}
	res = guestfs_aug_save(state->guestfs);
	if (res < 0) {
		fprintf(stderr, "Error: saving edited fstab failed.\n");
		goto cleanup;
	}
cleanup:
	guestfs_aug_close(state->guestfs);
	return res;
}

const struct {
	enum {
		SYSTEMD_MASK,
		SYSTEMD_DISABLE
	} action;
	char *unit;
} systemd_cleanups[] = {
	/* TODO: organize by category (e.g. network) and make them optional */
	{SYSTEMD_DISABLE, "networking.service"}, // ifup
	{SYSTEMD_DISABLE, "multipathd.service"}, // multipathd
	{SYSTEMD_MASK, "systemd-rfkill.socket"}, // systemd-rfkill
	// auditd: Only one instance should run, leave it to host
	// (https://access.redhat.com/articles/4494341)
	{SYSTEMD_DISABLE, "auditd.service"},
	{0},
};

static int cleanup_systemd(struct v2c_state *state) {
	for (int i = 0; systemd_cleanups[i].unit; i++) {
		char *const cmd[] = {
			"systemctl",
			systemd_cleanups[i].action == SYSTEMD_MASK ? "mask" : "disable",
			systemd_cleanups[i].unit,
			NULL,
		};
		free(guestfs_command(state->guestfs, cmd));
	}
	return 0;
}

static int setup_config(struct v2c_state *state, struct cvirt_oci_config *config) {
	int res;
	if (state->config.exec.user) {
		res = cvirt_oci_config_set_user(config, state->config.exec.user);
		if (res < 0) {
			return res;
		}
	}
	char *const default_cmd[] = {
		"/sbin/init",
		NULL,
	};
	res = cvirt_oci_config_set_cmd(config, state->config.exec.cmd ?
			(char *const *)state->config.exec.cmd : default_cmd);
	if (res < 0) {
		return res;
	}

	if (state->config.exec.entrypoint) {
		res = cvirt_oci_config_set_entrypoint(config,
				(char *const *)state->config.exec.entrypoint);
		if (res < 0) {
			return res;
		}
	}

	if (state->config.exec.env) {
		for (int i = 0; state->config.exec.env[i]; i++) {
			res = cvirt_oci_config_add_env(config, state->config.exec.env[i]);
			if (res < 0) {
				return res;
			}
		}
	}

	if (state->config.exec.workdir) {
		res = cvirt_oci_config_set_working_dir(config, state->config.exec.workdir);
		if (res < 0) {
			return res;
		}
	}

	if (!state->config.exec.cmd && !state->config.exec.entrypoint) {
		char *link = guestfs_readlink(state->guestfs, "/sbin/init");
		if (link) {
			int l = strlen(link);
			if (l >= 7 && !strncmp(&link[l - 7], "systemd", 7)) {
				res = cvirt_oci_config_set_stop_signal(config, "SIGRTMIN+3");
				if (res < 0) {
					return res;
				}
			}
			free(link);
		}
		res = cvirt_oci_config_set_stop_signal(config, "SIGPWR");
		if (res < 0) {
			return res;
		}
	}
	return res;
}

int main(int argc, char *argv[]) {
	struct v2c_state state = {
		.config =  {
			.compression = CVIRT_OCI_LAYER_COMPRESSION_ZSTD,
		},
	};
	if (parse_options(&state, argc, argv) < 0 || argc - optind != 2) {
		fprintf(stderr, usage, argv[0]);
		exit(EXIT_FAILURE);
	}

	char *source_date_epoch_env = getenv("SOURCE_DATE_EPOCH");
	if (source_date_epoch_env) {
		state.config.set_modification_epoch = true;
		state.config.source_date_epoch = atoll(source_date_epoch_env);
	}

	state.modification_start = time(NULL);

	char **succeeded_mounts;
	state.guestfs = create_guestfs_mount_first_linux(argv[optind], &succeeded_mounts);
	if (!state.guestfs) {
		exit(EXIT_FAILURE);
	}

	struct cvirt_oci_layer *layer = cvirt_oci_layer_new(state.config.compression,
		state.config.compression_level);
	if (!layer) {
		perror("cvirt_oci_layer_new");
		exit(EXIT_FAILURE);
	}

	state.layer_entry = archive_entry_new();
	if (!state.layer_entry) {
		perror("archive_entry_new");
		exit(EXIT_FAILURE);
	}

	state.layer_link_resolver = archive_entry_linkresolver_new();
	if (!state.layer_link_resolver) {
		perror("archive_entry_linkresolver_new");
		exit(EXIT_FAILURE);
	}
	state.layer_archive = cvirt_oci_layer_get_libarchive(layer);
	archive_entry_linkresolver_set_strategy(state.layer_link_resolver,
		archive_format(state.layer_archive));

	cleanup_fstab(&state, succeeded_mounts);

	for (int i = 0; succeeded_mounts[i]; i++) {
		free(succeeded_mounts[i]);
	}
	free(succeeded_mounts);

	if (!state.config.disable_systemd_cleanup) {
		cleanup_systemd(&state);
	}

	state.modification_end = time(NULL);

	// guestfs_tar_out would let whiteouts fall in
	dump_guestfs(&state, "/");

	archive_entry_free(state.layer_entry);
	archive_entry_linkresolver_free(state.layer_link_resolver);
	cvirt_oci_layer_close(layer);

	struct cvirt_oci_config *config = cvirt_oci_config_new();
	cvirt_oci_config_add_layer(config, layer);
	setup_config(&state, config);
	cvirt_oci_config_close(config);

	guestfs_umount_all(state.guestfs);
	guestfs_shutdown(state.guestfs);
	guestfs_close(state.guestfs);

	struct cvirt_oci_blob *config_blob = cvirt_oci_blob_from_config(config);
	struct cvirt_oci_blob *layer_blob = cvirt_oci_blob_from_layer(layer);
	struct cvirt_oci_manifest *manifest = cvirt_oci_manifest_new();
	cvirt_oci_manifest_set_config(manifest, config_blob);
	cvirt_oci_manifest_add_layer(manifest, layer_blob);
	cvirt_oci_manifest_close(manifest);

	struct cvirt_oci_blob *manifest_blob = cvirt_oci_blob_from_manifest(manifest);
	struct cvirt_oci_image *image = cvirt_oci_image_new(argv[optind + 1]);
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
	return 0;
}
