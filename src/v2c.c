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
#include <convirter/io/entry.h>
#include <convirter/io/xattr.h>
#include <convirter/oci/blob.h>
#include <convirter/oci/config.h>
#include <convirter/oci/image.h>
#include <convirter/oci/manifest.h>
#include <convirter/oci/layer.h>
#include <convirter/oci-r/config.h>
#include <convirter/oci-r/index.h>
#include <convirter/oci-r/layer.h>
#include <convirter/oci-r/manifest.h>
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
      --layer-reuse=ARCHIVE       Try to reuse layers from ARCHIVE\n\
      --skip-btrfs-snapshots      Skip btrfs snapshots\n\
\n\
Options below set respective config of output container image:\n"
COMMON_EXEC_CONFIG_OPTIONS_HELP;

static const int common_exec_config_start = 128;

static const struct option long_options[] = {
	{"compression",	required_argument,	NULL,	1},
	{"no-systemd-cleanup",	no_argument,	NULL,	2},
	{"layer-reuse",	required_argument,	NULL,	3},
	{"skip-btrfs-snapshots",no_argument,	NULL,	4},
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
		int layer_reuse_fd;
		bool skip_btrfs_snapshots;
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
		case 3:
			state->config.layer_reuse_fd = open(optarg, O_RDONLY | O_CLOEXEC);
			if (state->config.layer_reuse_fd == -1) {
				perror("open");
				exit(EXIT_FAILURE);
			}
			break;
		case 4:
			state->config.skip_btrfs_snapshots = true;
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

static const size_t ustar_logical_record_size = 512;

size_t new_entry(struct v2c_state *state, struct cvirt_io_entry *entry,
		const char *path, bool dry_run) {
	archive_entry_clear(state->layer_entry);
	archive_entry_set_pathname(state->layer_entry, &path[1]);
	struct stat *const stat = &entry->inode->stat;
	archive_entry_copy_stat(state->layer_entry, stat);

	// in pax, data is stored on first seen, and archive_entry_linkify
	// *sparse is for format where data is stored at last seen.
	struct archive_entry *dummy;
	archive_entry_linkify(state->layer_link_resolver, &state->layer_entry, &dummy);	
	if (dry_run) {
		size_t sz = ustar_logical_record_size;
		if (S_ISREG(entry->inode->stat.st_mode) && archive_entry_size(state->layer_entry)) {
			sz += (entry->inode->stat.st_size + ustar_logical_record_size - 1) /
				ustar_logical_record_size * ustar_logical_record_size;
		}
		// TODO consider pax extended header
		return sz;
	}

	if (S_ISLNK(stat->st_mode)) {
		archive_entry_set_symlink(state->layer_entry, entry->inode->target);
	}

	for (int i = 0; i < entry->inode->xattrs_len; i++) {
		archive_entry_xattr_add_entry(state->layer_entry,
			entry->inode->xattrs[i].name,
			entry->inode->xattrs[i].value,
			entry->inode->xattrs[i].len);
	}

	archive_write_header(state->layer_archive, state->layer_entry);

	// size is set to zero if linkify found hardlink to previous entry
	if (S_ISREG(stat->st_mode) && archive_entry_size(state->layer_entry)) {
		dump_file_content(state, path, stat->st_size);
	}
	return 0;
}

size_t new_whiteout_entry(struct v2c_state *state,
		const char *basepath, const char *name, bool dry_run) {
	if (dry_run) {
		return ustar_logical_record_size;
	}
	archive_entry_clear(state->layer_entry);
	char path[strlen(basepath) + strlen(name) + 5];
	strcpy(path, &basepath[1]); // trim /
	strcat(path, "/.wh.");
	strcat(path, name);
	archive_entry_set_pathname(state->layer_entry, path);
	archive_entry_set_filetype(state->layer_entry, AE_IFREG);
	archive_entry_set_size(state->layer_entry, 0);
	archive_write_header(state->layer_archive, state->layer_entry);
	return 0;
}

static bool compare_stat(struct stat *a, struct stat *b) {
	if (a->st_mode != b->st_mode) {
		return true;
	}
	if (a->st_uid != b->st_uid || a->st_gid != b->st_gid) {
		return true;
	}
	if ((S_ISCHR(a->st_mode) || S_ISBLK(a->st_mode)) && a->st_rdev != b->st_rdev) {
		return true;
	}
	if (S_ISREG(a->st_mode) && a->st_size != b->st_size) {
		return true;
	}
	if (a->st_mtime != b->st_mtime) {
		return true;
	}
	if (a->st_atime && b->st_atime && a->st_atime != b->st_atime) {
		return true;
	}
	return false;
}

static bool compare_xattr(struct cvirt_io_inode *a, struct cvirt_io_inode *b) {
	bool b_compared[b->xattrs_len];
	memset(b_compared, 0, sizeof(bool) * b->xattrs_len);
	for (int i = 0; i < a->xattrs_len; i++) {
		bool found = false;
		for (int j = 0; j < b->xattrs_len; j++) {
			if (b_compared[j]) {
				continue;
			}
			if (!strcmp(a->xattrs[i].name,
					b->xattrs[j].name)) {
				found = true;
				b_compared[j] = true;
				if (a->xattrs[i].len != b->xattrs[j].len ||
						memcmp(a->xattrs[i].value,
						b->xattrs[j].value, a->xattrs[i].len)) {
					return true;
				}
			}
		}
		if (!found) {
			return true;
		}
	}
	for (int j = 0; j < b->xattrs_len; j++) {
		if (!b_compared[j]) {
			return true;
		}
	}
	return false;
}

enum v2c_build_layer_mode {
	BUILD_LAYER_FULL,
	BUILD_LAYER_DRYRUN,
	BUILD_LAYER_TEST_DIR
};

size_t build_layer(struct cvirt_io_entry *a, struct cvirt_io_entry *b,
		const char *path, enum v2c_build_layer_mode mode, struct v2c_state *state) {
	bool differs = false;
	if (!a) {
		differs = true;
	} else {
		if (path[1] != '\0') { //TODO compare root?
			if (compare_stat(&a->inode->stat, &b->inode->stat) ||
					compare_xattr(a->inode, b->inode)) {
				differs = true;
			}
		}

		if (S_ISREG(a->inode->stat.st_mode)) {
			if (memcmp(a->inode->sha256sum, b->inode->sha256sum, 32)) {
				differs = true;;
			}
		} else if (S_ISLNK(a->inode->stat.st_mode)) {
			if (strcmp(a->inode->target, b->inode->target)) {
				differs = true;;
			}
		}
	}

	size_t layer_size = 0;
	if (S_ISDIR(b->inode->stat.st_mode)) {
		for (int i = 0; temporary_paths[i]; i++) { //TODO opt-out
			if (!strcmp(path, temporary_paths[i])) {
				// skip contents
				goto create_entry_if_differs;
			}
		}
		int b_match[b->inode->children_len];
		memset(b_match, 0, sizeof(int) * b->inode->children_len);
		bool b_create[b->inode->children_len];
		memset(b_create, 0, sizeof(bool) * b->inode->children_len);
		int a_len = (a && S_ISDIR(a->inode->stat.st_mode)) ? a->inode->children_len : 1;
		bool a_remove[a_len];
		memset(a_remove, 0, sizeof(bool) * a_len);
		int max_len = 0;

		if (a && S_ISDIR(a->inode->stat.st_mode)) {
			for (int i = 0; i < a->inode->children_len; i++) {
				int len = strlen(a->inode->children[i].name);
				max_len = len > max_len ? len : max_len;
			}
		}
		for (int i = 0; i < b->inode->children_len; i++) {
			int len = strlen(b->inode->children[i].name);
			max_len = len > max_len ? len : max_len;
		}
		int name_index = strlen(path) + 1;
		char npath[name_index + max_len + 1];
		if (path[1]) {
			strcpy(npath, path);
		} else {
			npath[0] = '\0';
			name_index = 1;
		}
		strcat(npath, "/");

		// on building: do a lightweight search, stop on first
		// child that need update, create dir, restart a full search
		// (prevent recurse everything twice)
		enum v2c_build_layer_mode recur_mode = (mode == BUILD_LAYER_FULL) ?
			BUILD_LAYER_TEST_DIR : mode;
		if (a && S_ISDIR(a->inode->stat.st_mode)) {
			for (int i = 0; i < a->inode->children_len; i++) {
				bool found = false;
				for (int j = 0; j < b->inode->children_len; j++) {
					if (b_match[j]) {
						continue;
					}
					if (!strcmp(a->inode->children[i].name,
							b->inode->children[j].name)) {
						found = true;
						b_match[j] = i + 1;
						strcpy(&npath[name_index], b->inode->children[j].name);
						size_t res = build_layer(
							&a->inode->children[i],
							&b->inode->children[j],
							npath, recur_mode, state);
						if (res) {
							if (mode == BUILD_LAYER_TEST_DIR) {
								return true;
							}
							b_create[j] = true;
							layer_size += res;
						}
						break;
					}
				}
				if (!found) {
					if (mode == BUILD_LAYER_TEST_DIR) {
						return true;
					}
					a_remove[i] = true;
					layer_size += new_whiteout_entry(state, path, a->inode->children[i].name, true);
				}
			}
		}
		for (int j = 0; j < b->inode->children_len; j++) {
			if (!strncmp(b->inode->children[j].name, ".wh.", 4)) {
				// no way to store names like whiteouts in OCI
				continue;
			}
			if (!b_match[j]) {
				if (mode == BUILD_LAYER_TEST_DIR) {
					return true;
				}
				b_create[j] = true;
				strcpy(&npath[name_index], b->inode->children[j].name);
				layer_size += build_layer(NULL, &b->inode->children[j], npath, recur_mode, state);
			}
		}
		if (layer_size) {
			// directory itself
			layer_size += new_entry(state, b, path, mode != BUILD_LAYER_FULL);
			if (mode == BUILD_LAYER_FULL) { // apply changes
				if (a) {
					for (int i = 0; i < a->inode->children_len; i++) {
						if (a_remove[i]) {
							new_whiteout_entry(state, path, a->inode->children[i].name, false);
						}
					}
				}
				for (int i = 0; i < b->inode->children_len; i++) {
					if (b_create[i]) {
						strcpy(&npath[name_index], b->inode->children[i].name);
						build_layer(b_match[i] ? &a->inode->children[b_match[i] - 1] : NULL,
							&b->inode->children[i], npath, mode, state);
					}
				}
			}
			return layer_size;
		}
	}

create_entry_if_differs:
	if (differs) {
		if (mode == BUILD_LAYER_TEST_DIR) {
			return true;
		}
		return new_entry(state, b, path, mode != BUILD_LAYER_FULL);
	}
	return 0;
}

void timestamp_fixup(struct cvirt_io_entry *tree, struct v2c_state *state) {
	struct stat *stat = &tree->inode->stat;
	if (stat->st_atim.tv_sec >= state->modification_start &&
			stat->st_atim.tv_sec <= state->modification_end) {
		stat->st_atim.tv_sec = state->config.source_date_epoch;
		stat->st_atim.tv_nsec = 0;
	}
	if (stat->st_mtim.tv_sec >= state->modification_start &&
			stat->st_mtim.tv_sec <= state->modification_end) {
		stat->st_mtim.tv_sec = state->config.source_date_epoch;
		stat->st_mtim.tv_nsec = 0;
	}
	if (stat->st_ctim.tv_sec >= state->modification_start &&
			stat->st_ctim.tv_sec <= state->modification_end) {
		stat->st_ctim.tv_sec = state->config.source_date_epoch;
		stat->st_ctim.tv_nsec = 0;
	}

	if (S_ISDIR(stat->st_mode)) {
		for (int i = 0; i < tree->inode->children_len; i++) {
			timestamp_fixup(&tree->inode->children[i], state);
		}
	}
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

	state.layer_archive = cvirt_oci_layer_get_libarchive(layer);
	state.layer_entry = archive_entry_new();
	if (!state.layer_entry) {
		perror("archive_entry_new");
		exit(EXIT_FAILURE);
	}

	cleanup_fstab(&state, succeeded_mounts);

	for (int i = 0; succeeded_mounts[i]; i++) {
		free(succeeded_mounts[i]);
	}
	free(succeeded_mounts);

	if (!state.config.disable_systemd_cleanup) {
		cleanup_systemd(&state);
	}

	state.modification_end = time(NULL);

	uint32_t flags = 0;
	if (state.config.skip_btrfs_snapshots) {
		flags |= CVIRT_IO_TREE_GUESTFS_BTRFS_SKIP_SNAPSHOTS;
	}
	struct cvirt_io_entry *guestfs_tree = cvirt_io_tree_from_guestfs(state.guestfs, flags);

	if (state.config.set_modification_epoch) {
		timestamp_fixup(guestfs_tree, &state);
	}

	struct cvirt_oci_image *image = cvirt_oci_image_new(argv[optind + 1]);
	struct cvirt_oci_manifest *manifest = cvirt_oci_manifest_new();
	struct cvirt_oci_config *config = cvirt_oci_config_new();

	struct cvirt_io_entry *reused_tree = NULL;
	size_t reused = 0;
	if (state.config.layer_reuse_fd) {
		state.layer_link_resolver = archive_entry_linkresolver_new();
		archive_entry_linkresolver_set_strategy(state.layer_link_resolver,
			archive_format(state.layer_archive));
		size_t baseline = build_layer(NULL, guestfs_tree, "/", BUILD_LAYER_DRYRUN, &state) +
			2 * ustar_logical_record_size; // 2 blocks of end-of-archive indicator
		archive_entry_linkresolver_free(state.layer_link_resolver);

		struct cvirt_oci_r_index *index =
			cvirt_oci_r_index_from_archive(state.config.layer_reuse_fd);
		const char *manifest_digest =
			cvirt_oci_r_index_get_native_manifest_digest(index);
		struct cvirt_oci_r_manifest *from_manifest =
			cvirt_oci_r_manifest_from_archive_blob(
				state.config.layer_reuse_fd, manifest_digest);
		int len = cvirt_oci_r_manifest_get_layers_length(from_manifest);

		struct cvirt_io_entry *tree;

		struct cvirt_oci_r_layer *layer = cvirt_oci_r_layer_from_archive_blob(
			state.config.layer_reuse_fd,
			cvirt_oci_r_manifest_get_layer_digest(from_manifest, 0),
			cvirt_oci_r_manifest_get_layer_compression(from_manifest, 0));
		tree = cvirt_io_tree_from_oci_layer(layer, 0);
		for (int i = 1; i < len; i++) {
			struct cvirt_oci_r_layer *layer = cvirt_oci_r_layer_from_archive_blob(
				state.config.layer_reuse_fd,
				cvirt_oci_r_manifest_get_layer_digest(from_manifest, i),
				cvirt_oci_r_manifest_get_layer_compression(from_manifest, i));
			cvirt_io_tree_oci_apply_layer(tree, layer, 0);
		}

		state.layer_link_resolver = archive_entry_linkresolver_new();
		archive_entry_linkresolver_set_strategy(state.layer_link_resolver,
			archive_format(state.layer_archive));
		reused = build_layer(tree, guestfs_tree, "/", BUILD_LAYER_DRYRUN, &state);
		if (reused) {
			reused += 2 * ustar_logical_record_size;
		}
		archive_entry_linkresolver_free(state.layer_link_resolver);

		printf("Estimated layer size without reuse: %ld, with reuse: %ld\n", baseline, reused);

		if (reused < baseline) {
			struct cvirt_oci_r_config *from_config = cvirt_oci_r_config_from_archive_blob(
				state.config.layer_reuse_fd, cvirt_oci_r_manifest_get_config_digest(from_manifest));
			for (int i = 0; i < len; i++) {
				struct cvirt_oci_layer * layer = cvirt_oci_layer_from_archive_blob(
					state.config.layer_reuse_fd,
					cvirt_oci_r_manifest_get_layer_digest(from_manifest, i),
					cvirt_oci_r_manifest_get_layer_compression(from_manifest, i),
					cvirt_oci_r_config_get_diff_id(from_config, i));
				struct cvirt_oci_blob *layer_blob = cvirt_oci_blob_from_layer(layer);
				cvirt_oci_config_add_layer(config, layer);
				cvirt_oci_manifest_add_layer(manifest, layer_blob);
				cvirt_oci_image_add_blob(image, layer_blob);
				cvirt_oci_blob_destory(layer_blob);
				cvirt_oci_layer_destroy(layer);
			}
			cvirt_oci_r_config_destroy(from_config);
			reused_tree = tree;
		}
	}

	if (!reused_tree || reused) {
		state.layer_link_resolver = archive_entry_linkresolver_new();
		archive_entry_linkresolver_set_strategy(state.layer_link_resolver,
			archive_format(state.layer_archive));

		build_layer(reused_tree, guestfs_tree, "/", BUILD_LAYER_FULL, &state);
		cvirt_io_tree_destroy(guestfs_tree);

		archive_entry_free(state.layer_entry);
		archive_entry_linkresolver_free(state.layer_link_resolver);
		cvirt_oci_layer_close(layer);

		struct cvirt_oci_blob *layer_blob = cvirt_oci_blob_from_layer(layer);
		cvirt_oci_config_add_layer(config, layer);
		cvirt_oci_manifest_add_layer(manifest, layer_blob);
		cvirt_oci_image_add_blob(image, layer_blob);
		cvirt_oci_blob_destory(layer_blob);
		cvirt_oci_layer_destroy(layer);
	}

	setup_config(&state, config);
	cvirt_oci_config_close(config);

	guestfs_umount_all(state.guestfs);
	guestfs_shutdown(state.guestfs);
	guestfs_close(state.guestfs);

	struct cvirt_oci_blob *config_blob = cvirt_oci_blob_from_config(config);
	cvirt_oci_manifest_set_config(manifest, config_blob);
	cvirt_oci_image_add_blob(image, config_blob);
	cvirt_oci_blob_destory(config_blob);
	cvirt_oci_config_destroy(config);

	cvirt_oci_manifest_close(manifest);

	struct cvirt_oci_blob *manifest_blob = cvirt_oci_blob_from_manifest(manifest);
	cvirt_oci_image_add_manifest(image, manifest_blob);
	cvirt_oci_image_close(image);
	cvirt_oci_blob_destory(manifest_blob);
	cvirt_oci_manifest_destroy(manifest);

	cvirt_oci_image_destroy(image);
	return 0;
}
