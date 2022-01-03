#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <convirter/mtree/entry.h>
#include <convirter/oci-r/config.h>
#include <convirter/oci-r/index.h>
#include <convirter/oci-r/layer.h>
#include <convirter/oci-r/manifest.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <guestfs.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include "../common/guestfs.h"
#include "../common/common-config.h"

static const char usage[] = "\
Usage: %s [OPTION]... INPUT OUTPUT\n\
Convert OCI container image to VM disk image for use with kernel and initramfs\n\
from c2v-mkboot.\n\
\n\
Options below overrides what is read from container image:\n"
COMMON_EXEC_CONFIG_OPTIONS_HELP;

static const int common_exec_config_start = 128;

static const struct option long_options[] = {
	COMMON_EXEC_CONFIG_LONG_OPTIONS(128),
	{0},
};

static const int bufsz = 4000 * 1024;

#define C2V_DIR	"/.c2v"
#define C2V_INIT	C2V_DIR "/init"
#define C2V_LAYERS	C2V_DIR "/layers"

struct c2v_state {
	guestfs_h *guestfs;
	struct archive *archive;
	struct archive_entry *archive_entry;
	struct {
		time_t source_date_epoch;
		bool set_modification_epoch;
		struct common_exec_config exec;
	} config;
};

static int parse_options(struct c2v_state *state, int argc, char *argv[]) {
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
		}
	}
	return 0;
}

static int set_attr(struct c2v_state *state, const char *path,
		const struct stat *stat) {
	int res = guestfs_lchown(state->guestfs, stat->st_uid, stat->st_gid, path);
	if (res < 0) {
		return res;
	}
	res = guestfs_utimens(state->guestfs, path, stat->st_atim.tv_sec,
		stat->st_atim.tv_nsec, stat->st_mtim.tv_sec, stat->st_mtim.tv_nsec);
	if (res < 0) {
		return res;
	}
	if ((!S_ISLNK(stat->st_mode)) && (stat->st_mode & 07000)) {
		guestfs_chmod(state->guestfs, stat->st_mode & 07777, path);
	}
	const char *name;
	const void *val;
	size_t sz;
	int count = archive_entry_xattr_count(state->archive_entry);
	archive_entry_xattr_reset(state->archive_entry);
	for (int i = 0; i < count; i++) {
		archive_entry_xattr_next(state->archive_entry, &name, &val, &sz);
		guestfs_lsetxattr(state->guestfs, name, val, sz, path);
	}
	return res;
}

static int dump_file(struct c2v_state *state, const char *path,
		const struct stat *stat) {
	int res = guestfs_truncate_size(state->guestfs, path, stat->st_size);
	if (res < 0) {
		return res;
	}
	const char *cbuf;
	const void *buf;
	size_t size;
	off_t offset;
	while (archive_read_data_block(state->archive, &buf, &size, &offset) != ARCHIVE_EOF) {
		cbuf = buf;
		int buf_offset = 0;
		while (size > bufsz) {
			res = guestfs_pwrite(state->guestfs, path, &cbuf[buf_offset], bufsz, offset);
			if (res < 0) {
				return res;
			}
			offset += bufsz;
			buf_offset += bufsz;
			size -= bufsz;
		}
		res = guestfs_pwrite(state->guestfs, path, &cbuf[buf_offset], size, offset);
		if (res < 0) {
			return res;
		}
	}
	return res;
}

static int apply_whiteouts(struct c2v_state *state) {
	int res = archive_read_next_header(state->archive, &state->archive_entry);
	while (res != ARCHIVE_EOF && res != ARCHIVE_FATAL) {
		char *basename_dup = NULL, *dirname_dup = NULL;
		if (res == ARCHIVE_WARN) {
			fprintf(stderr, "warning: %s: %s\n",
				archive_entry_pathname(state->archive_entry),
				archive_error_string(state->archive));
		} else if (res == ARCHIVE_RETRY) {
			fprintf(stderr, "warning: %s: %s, retry\n",
				archive_entry_pathname(state->archive_entry),
				archive_error_string(state->archive));
			goto next;
		}
		const char *path = archive_entry_pathname(state->archive_entry);
		if (!strncmp(path, ".c2v/", 4) || !strcmp(path, ".wh..c2v")) {
			goto next;
		}
		basename_dup = strdup(path);
		assert(basename_dup);
		const char *name = basename(basename_dup);
		assert(name);

		if (strncmp(name, ".wh.", 4)) {
			// not a whiteout
			goto next;
		}

		const char *dir = dirname(dirname_dup);
		assert(dir);

		if (!strcmp(name, ".wh..wh..opq")) {
			// opaque whiteout
			char *abs_path = calloc(strlen(dir) + 2, sizeof(char));
			assert(abs_path);
			abs_path[0] = '/';
			if (!(dir[0] == '.' && !dir[1])) { // not just .
				strcpy(&abs_path[1], dir);
			}

			int i = 0;
			char **ls = guestfs_ls(state->guestfs, abs_path);
			if (!ls) {
				goto opq_cleanup;
			}
			for (; ls[i]; i++) {
				char *full;
				if (abs_path[1]) { // not /
					full = calloc(strlen(abs_path) + strlen(ls[i]) + 2, sizeof(char));
					assert(full);
					strcpy(full, abs_path);
					strcat(full, "/");
					strcat(full, ls[i]);
				} else {
					if (strcmp(ls[i], ".c2v")) {
						free(ls[i]);
						continue;
					}
					full = calloc(strlen(ls[i]) + 2, sizeof(char));
					assert(full);
					full[0] = '/';
					strcpy(&full[1], ls[i]);
				}
				free(ls[i]);
				int res = guestfs_rm_rf(state->guestfs, full);
				free(full);
				if (res < 0) {
					return res;
				}
			}

		opq_cleanup:
			free(ls);
			free(abs_path);
			goto next;
		}

		// explicit whiteout
		int dir_len = strlen(dir);
		char *full = calloc(1 + dir_len + 1 + strlen(name) - 4 + 1, sizeof(char));
		assert(full);
		full[0] = '/';
		strcpy(&full[1], dir);
		full[dir_len + 1] = '/';
		strcpy(&full[dir_len + 2], &name[4]);
		res = guestfs_rm_rf(state->guestfs, full);
		free(full);
		if (res < 0) {
			return res;
		}
next:
		free(basename_dup);
		free(dirname_dup);
		res = archive_read_next_header(state->archive, &state->archive_entry);
	}
	if (res == ARCHIVE_FATAL) {
		fprintf(stderr, "fatal: %s\n", archive_error_string(state->archive));
		return -archive_errno(state->archive);
	}
	return 0;
}

static int dump_layer(struct c2v_state *state) {
	int res = archive_read_next_header(state->archive, &state->archive_entry);
	while (res != ARCHIVE_EOF && res != ARCHIVE_FATAL) {
		if (res == ARCHIVE_WARN) {
			fprintf(stderr, "warning: %s: %s\n",
				archive_entry_pathname(state->archive_entry),
				archive_error_string(state->archive));
		} else if (res == ARCHIVE_RETRY) {
			fprintf(stderr, "warning: %s: %s, retry\n",
				archive_entry_pathname(state->archive_entry),
				archive_error_string(state->archive));
			goto next;
		}
		const char *path = archive_entry_pathname(state->archive_entry);
		if (!strcmp(path, ".c2v")) {
			fprintf(stderr, "warning: layer: skipping our special path\n");
			goto next;
		}
		char *basename_dup = strdup(path);
		assert(basename_dup);
		const char *name = basename(basename_dup);
		assert(name);
		if (!strncmp(name, ".wh.", 4)) {
			// whiteouts
			free(basename_dup);
			goto next;
		}
		free(basename_dup);

		char *abs_path = calloc(2 + strlen(path), sizeof(char));
		assert(abs_path);
		abs_path[0] = '/';
		strcpy(&abs_path[1], path);

		if (strlen(abs_path) > 1 && !strcmp(&abs_path[strlen(abs_path) - 1], "/.")) {
			abs_path[strlen(abs_path) - 1] = '\0';
		}

		const char *hardlink = archive_entry_hardlink(state->archive_entry);
		if (hardlink) {
			char *abs_target = calloc(2 + strlen(hardlink),
				sizeof(char));
			abs_target[0] = '/';
			strcpy(&abs_target[1], hardlink);
			int res = guestfs_ln(state->guestfs, abs_target, abs_path);
			if (res < 0) {
				return res;
			}
			free(abs_target);
			free(abs_path);
			goto next;
		}

		const struct stat *stat = archive_entry_stat(state->archive_entry);

		if ((stat->st_mode & S_IFMT) != S_IFDIR) {
			// overwrites
			assert(guestfs_rm_rf(state->guestfs, abs_path) >= 0);
		} else {
			// try to remove non-dir if exists
			guestfs_rm_f(state->guestfs, abs_path);
		}

		switch (stat->st_mode & S_IFMT) {
		case S_IFLNK:
			res = guestfs_ln_s(state->guestfs,
				archive_entry_symlink(state->archive_entry), abs_path);
			break;
		case S_IFREG:
			res = guestfs_mknod(state->guestfs, stat->st_mode, 0, 0,
				abs_path);
			if (res < 0) {
				break;
			}
			res = dump_file(state, abs_path, stat);
			break;
		case S_IFDIR:
			if (guestfs_is_dir(state->guestfs, abs_path)) {
				res = guestfs_chmod(state->guestfs,
					stat->st_mode, abs_path);
			} else {
				res = guestfs_mkdir_mode(state->guestfs,
					abs_path, stat->st_mode);
			}
			break;
		case S_IFSOCK:
		case S_IFBLK:
		case S_IFCHR:
		case S_IFIFO:
			res = guestfs_mknod(state->guestfs, stat->st_mode,
				major(stat->st_rdev), minor(stat->st_rdev),
				abs_path);
			break;
		default:
			fprintf(stderr, "dump_layer: unrecognized file type at %s\n", path);
			goto next;
		}

		if (res < 0) {
			return res;
		}
		res = set_attr(state, abs_path, stat);
		if (res < 0) {
			return res;
		}
		free(abs_path);
next:
		res = archive_read_next_header(state->archive, &state->archive_entry);
	}
	if (res == ARCHIVE_FATAL) {
		fprintf(stderr, "fatal: %s\n", archive_error_string(state->archive));
		return -archive_errno(state->archive);
	}
	return 0;
}

static int append_quoted_string(struct c2v_state *state, const char *path,
		const char *string) {
	const char escaped_quote[] = "'\\''";
	int escaped_quote_len = strlen(escaped_quote);
	int len = strlen(string), k = 0, size = 2 + len;
	for (int i = 0; i < len; i++) {
		if (string[i] == '\'') {
			size += escaped_quote_len;
		}
	}
	char buffer[size];
	buffer[0] = '\'';
	int index = 1;
	for (int i = 0; i < len; i++) {
		if (string[i] == '\'') {
			int part_len = i - k;
			if (part_len) {
				strncpy(&buffer[index], &string[k], part_len);
				index += part_len;
			}
			strncpy(&buffer[index], escaped_quote, escaped_quote_len);
			index += escaped_quote_len;
			k = i + 1;
		}
	}
	int part_len = len - k;
	if (part_len) {
		strncpy(&buffer[index], &string[k], part_len);
	}
	buffer[size - 1] = '\'';
	return guestfs_write_append(state->guestfs, path, buffer, size);
}

static int generate_init_script(struct c2v_state *state,
		struct cvirt_oci_r_config *config) {
	int res;
	int env_count = cvirt_oci_r_config_get_env_length(config);
	for (int i = 0; i < env_count; i++) {
		const char export_pre[] = "export ";
		res = guestfs_write_append(state->guestfs, C2V_INIT,
			export_pre, strlen(export_pre));
		if (res < 0) {
			return res;
		}
		const char *env = cvirt_oci_r_config_get_env(config, i);
		res = append_quoted_string(state, C2V_INIT, env);
		if (res < 0) {
			return res;
		}
		res = guestfs_write_append(state->guestfs, C2V_INIT, "\n", 1);
		if (res < 0) {
			return res;
		}
	}

	if (state->config.exec.env) {
		for (int i = 0; state->config.exec.env[i]; i++) {
			const char export_pre[] = "export ";
			res = guestfs_write_append(state->guestfs, C2V_INIT,
				export_pre, strlen(export_pre));
			if (res < 0) {
				return res;
			}
			res = append_quoted_string(state, C2V_INIT,
				state->config.exec.env[i]);
			if (res < 0) {
				return res;
			}
			res = guestfs_write_append(state->guestfs, C2V_INIT, "\n", 1);
			if (res < 0) {
				return res;
			}
		}
	}

	const char *workdir = state->config.exec.workdir ? state->config.exec.workdir :
		cvirt_oci_r_config_get_working_dir(config);
	if (workdir) {
		const char workdir_pre[] = "_WORKDIR=";
		res = guestfs_write_append(state->guestfs, C2V_INIT,
			workdir_pre, strlen(workdir_pre));
		if (res < 0) {
			return res;
		}
		res = append_quoted_string(state, C2V_INIT, workdir);
		if (res < 0) {
			return res;
		}
		res = guestfs_write_append(state->guestfs, C2V_INIT, "\n", 1);
		if (res < 0) {
			return res;
		}
	}

	const char *user = state->config.exec.user ? state->config.exec.user :
		cvirt_oci_r_config_get_user(config);
	if (user) {
		const char user_pre[] = "_UIDGID=";
		res = guestfs_write_append(state->guestfs, C2V_INIT,
			user_pre, strlen(user_pre));
		if (res < 0) {
			return res;
		}
		res = append_quoted_string(state, C2V_INIT, user);
		if (res < 0) {
			return res;
		}
		res = guestfs_write_append(state->guestfs, C2V_INIT, "\n", 1);
		if (res < 0) {
			return res;
		}
	}

	int entrypoint_len = cvirt_oci_r_config_get_entrypoint_length(config),
		cmd_len = cvirt_oci_r_config_get_cmd_length(config);
	if (entrypoint_len || cmd_len) {
		const char pre_cmd[] = "set -- ";
		res = guestfs_write_append(state->guestfs, C2V_INIT, pre_cmd, strlen(pre_cmd));
		if (res < 0) {
			return res;
		}
		if (state->config.exec.entrypoint) {
			for (int i = 0; state->config.exec.entrypoint[i]; i++) {
				res = append_quoted_string(state, C2V_INIT,
					state->config.exec.entrypoint[i]);
				if (res < 0) {
					return res;
				}
				res = guestfs_write_append(state->guestfs, C2V_INIT, " ", 1);
				if (res < 0) {
					return res;
				}
			}
		} else {
			for (int i = 0; i < entrypoint_len; i++) {
				const char *part = cvirt_oci_r_config_get_entrypoint_part(config, i);
				res = append_quoted_string(state, C2V_INIT, part);
				if (res < 0) {
					return res;
				}
				res = guestfs_write_append(state->guestfs, C2V_INIT, " ", 1);
				if (res < 0) {
					return res;
				}
			}
		}

		if (state->config.exec.cmd) {
			for (int i = 0; state->config.exec.cmd[i]; i++) {
				res = append_quoted_string(state, C2V_INIT,
					state->config.exec.cmd[i]);
				if (res < 0) {
					return res;
				}
				res = guestfs_write_append(state->guestfs, C2V_INIT, " ", 1);
				if (res < 0) {
					return res;
				}
			}
		} else {
			for (int i = 0; i < cmd_len; i++) {
				const char *part = cvirt_oci_r_config_get_cmd_part(config, i);
				res = append_quoted_string(state, C2V_INIT, part);
				if (res < 0) {
					return res;
				}
				res = guestfs_write_append(state->guestfs, C2V_INIT, " ", 1);
				if (res < 0) {
					return res;
				}
			}
		}
		res = guestfs_write_append(state->guestfs, C2V_INIT, "\n", 1);
		if (res < 0) {
			return res;
		}
	}

	res = guestfs_chmod(state->guestfs, 0400, C2V_INIT);
	if (res < 0) {
		return res;
	}

	if (state->config.set_modification_epoch) {
		res = guestfs_utimens(state->guestfs, C2V_INIT,
			state->config.source_date_epoch, 0,
			state->config.source_date_epoch, 0);
		if (res < 0) {
			return res;
		}
		res = guestfs_utimens(state->guestfs, C2V_DIR,
			state->config.source_date_epoch, 0,
			state->config.source_date_epoch, 0);
		if (res < 0) {
			return res;
		}
		res = guestfs_utimens(state->guestfs, "/",
			state->config.source_date_epoch, 0,
			state->config.source_date_epoch, 0);
	}
	return res;
}

static int block_sz = 4096;

static size_t estimate_disk_usage(const struct cvirt_mtree_entry *entry) {
	if (S_ISREG(entry->inode->stat.st_mode)) {
		return (entry->inode->stat.st_size + block_sz - 1) / block_sz * block_sz;
	} else if (S_ISDIR(entry->inode->stat.st_mode)) {
		size_t total = 0;
		for (int i = 0; i < entry->inode->children_len; i++) {
			total += estimate_disk_usage(&entry->inode->children[i]);
		}
		return total;
	}
	return 0;
}

static void restore_mtime(struct c2v_state *state, struct cvirt_mtree_entry *entry,
		const char *path) {
	for (int i = 0; i < entry->inode->children_len; i++) {
		if (!S_ISDIR(entry->inode->children[i].inode->stat.st_mode)) {
			continue;
		}
		char mpath[strlen(path) + strlen(entry->inode->children[i].name) + 2];
		strcpy(mpath, path);
		strcat(mpath, entry->inode->children[i].name);
		strcat(mpath, "/");
		restore_mtime(state, &entry->inode->children[i], mpath);
	}

	guestfs_utimens(state->guestfs, path, entry->inode->stat.st_atim.tv_sec,
		entry->inode->stat.st_atim.tv_nsec, entry->inode->stat.st_mtim.tv_sec,
		entry->inode->stat.st_mtim.tv_nsec);
}

int main(int argc, char *argv[]) {
	struct c2v_state global_state = {0};
	if (parse_options(&global_state, argc, argv) < 0 || argc - optind != 2) {
		fprintf(stderr, usage, argv[0]);
		exit(EXIT_FAILURE);
	}

	char *source_date_epoch_env = getenv("SOURCE_DATE_EPOCH");
	if (source_date_epoch_env) {
		global_state.config.set_modification_epoch = true;
		global_state.config.source_date_epoch = atoll(source_date_epoch_env);
	}

	int fd = open(argv[optind], O_RDONLY | O_CLOEXEC);
	assert(fd != -1);
	struct cvirt_oci_r_index *index = cvirt_oci_r_index_from_archive(fd);
	const char *manifest_digest = cvirt_oci_r_index_get_native_manifest_digest(index);
	struct cvirt_oci_r_manifest *manifest = cvirt_oci_r_manifest_from_archive_blob(fd, manifest_digest);
	const char *config_digest = cvirt_oci_r_manifest_get_config_digest(manifest);
	int len = cvirt_oci_r_manifest_get_layers_length(manifest);

	struct cvirt_mtree_entry *layer_trees[len];

	struct cvirt_oci_r_layer *layer = cvirt_oci_r_layer_from_archive_blob(
		fd, cvirt_oci_r_manifest_get_layer_digest(manifest, 0),
		cvirt_oci_r_manifest_get_layer_compression(manifest, 0));
	layer_trees[0] = cvirt_mtree_tree_from_oci_layer(layer, 0);
	size_t needed = estimate_disk_usage(layer_trees[0]);
	cvirt_oci_r_layer_destroy(layer);
	for (int i = 1; i < len; i++) {
		const char *layer_digest = cvirt_oci_r_manifest_get_layer_digest(manifest, i);
		struct cvirt_oci_r_layer *layer =
			cvirt_oci_r_layer_from_archive_blob(fd, layer_digest,
			cvirt_oci_r_manifest_get_layer_compression(manifest, i));
		layer_trees[i] = cvirt_mtree_tree_from_oci_layer(layer, 0);
		needed += estimate_disk_usage(layer_trees[i]);
		cvirt_oci_r_layer_destroy(layer);
	}

	size_t image_size = (needed * 2) < 114294784 ? 114294784 : (needed * 2);
	global_state.guestfs = create_qcow2_btrfs_image(argv[optind + 1], image_size);
	if (!global_state.guestfs) {
		exit(EXIT_FAILURE);
	}
	assert(guestfs_umask(global_state.guestfs, 0) >= 0);
	
	assert(guestfs_mkdir_mode(global_state.guestfs, C2V_DIR, 0500) >= 0);
	assert(guestfs_mkdir_mode(global_state.guestfs, C2V_LAYERS, 0500) >= 0);
	if (global_state.config.set_modification_epoch) {
		assert(guestfs_utimens(global_state.guestfs, C2V_LAYERS,
			global_state.config.source_date_epoch, 0,
			global_state.config.source_date_epoch, 0) >= 0);
		assert(guestfs_utimens(global_state.guestfs, C2V_DIR,
			global_state.config.source_date_epoch, 0,
			global_state.config.source_date_epoch, 0) >= 0);
		assert(guestfs_utimens(global_state.guestfs, "/",
			global_state.config.source_date_epoch, 0,
			global_state.config.source_date_epoch, 0) >= 0);
	}
	assert(guestfs_btrfs_subvolume_snapshot_opts(global_state.guestfs, "/",
		C2V_LAYERS "/base",
		GUESTFS_BTRFS_SUBVOLUME_SNAPSHOT_OPTS_RO, 1, -1) >= 0);
	if (global_state.config.set_modification_epoch) {
		assert(guestfs_utimens(global_state.guestfs, C2V_LAYERS,
			global_state.config.source_date_epoch, 0,
			global_state.config.source_date_epoch, 0) >= 0);
		assert(guestfs_utimens(global_state.guestfs, C2V_DIR,
			global_state.config.source_date_epoch, 0,
			global_state.config.source_date_epoch, 0) >= 0);
		assert(guestfs_utimens(global_state.guestfs, "/",
			global_state.config.source_date_epoch, 0,
			global_state.config.source_date_epoch, 0) >= 0);
	}

	for (int i = 0; i < len; i++) {
		const char *layer_digest = cvirt_oci_r_manifest_get_layer_digest(manifest, i);
		struct cvirt_oci_r_layer *layer =
			cvirt_oci_r_layer_from_archive_blob(fd, layer_digest,
			cvirt_oci_r_manifest_get_layer_compression(manifest, i));

		// first pass: whiteouts
		global_state.archive = cvirt_oci_r_layer_get_libarchive(layer);
		apply_whiteouts(&global_state);

		// second pass: data
		cvirt_oci_r_layer_rewind(layer);
		global_state.archive = cvirt_oci_r_layer_get_libarchive(layer);
		dump_layer(&global_state);
		cvirt_oci_r_layer_destroy(layer);

		restore_mtime(&global_state, layer_trees[i], "/");

		cvirt_mtree_tree_destroy(layer_trees[i]);

		// snapshot after each layer
		char *snapshot_dest = calloc(strlen(layer_digest) + 14, sizeof(char));
		assert(snapshot_dest);
		strcpy(snapshot_dest, C2V_LAYERS "/");
		strcat(snapshot_dest, layer_digest);
		assert(guestfs_btrfs_subvolume_snapshot_opts(global_state.guestfs, "/",
			snapshot_dest,
			GUESTFS_BTRFS_SUBVOLUME_SNAPSHOT_OPTS_RO, 1, -1) >= 0);
		if (global_state.config.set_modification_epoch) {
			assert(guestfs_utimens(global_state.guestfs, C2V_LAYERS,
				global_state.config.source_date_epoch, 0,
				global_state.config.source_date_epoch, 0) >= 0);
			assert(guestfs_utimens(global_state.guestfs, C2V_DIR,
				global_state.config.source_date_epoch, 0,
				global_state.config.source_date_epoch, 0) >= 0);
			assert(guestfs_utimens(global_state.guestfs, "/",
				global_state.config.source_date_epoch, 0,
				global_state.config.source_date_epoch, 0) >= 0);
		}
		free(snapshot_dest);
	}
	struct cvirt_oci_r_config *config =
		cvirt_oci_r_config_from_archive_blob(fd, config_digest);
	generate_init_script(&global_state, config);
	cvirt_oci_r_config_destroy(config);
	cvirt_oci_r_manifest_destroy(manifest);
	cvirt_oci_r_index_destroy(index);
	close(fd);

	guestfs_umount_all(global_state.guestfs);
	guestfs_shutdown(global_state.guestfs);
	guestfs_close(global_state.guestfs);
	return 0;
}
