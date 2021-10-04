#include <archive.h>
#include <archive_entry.h>
#include <assert.h>
#include <convirter/io/entry.h>
#include <convirter/oci-r/config.h>
#include <convirter/oci-r/index.h>
#include <convirter/oci-r/layer.h>
#include <convirter/oci-r/manifest.h>
#include <fcntl.h>
#include <guestfs.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include "../common/guestfs.h"

static const int bufsz = 4000 * 1024;

#define C2V_DIR	"/.c2v"
#define C2V_INIT	C2V_DIR "/init"
#define C2V_BUSYBOX	C2V_DIR "/busybox"
#define C2V_LAYERS	C2V_DIR "/layers"

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

static int apply_whiteouts(guestfs_h *guestfs, struct archive *archive) {
	struct archive_entry *entry;
	int res = archive_read_next_header(archive, &entry);
	while (res != ARCHIVE_EOF && res != ARCHIVE_FATAL) {
		char *basename_dup = NULL, *dirname_dup = NULL;
		if (res == ARCHIVE_WARN) {
			fprintf(stderr, "warning: %s: %s\n",
				archive_entry_pathname(entry),
				archive_error_string(archive));
		} else if (res == ARCHIVE_RETRY) {
			fprintf(stderr, "warning: %s: %s, retry\n",
				archive_entry_pathname(entry),
				archive_error_string(archive));
			goto next;
		}
		const char *path = archive_entry_pathname(entry);
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
			char **ls = guestfs_ls(guestfs, abs_path);
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
				int res = guestfs_rm_rf(guestfs, full);
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
		res = guestfs_rm_rf(guestfs, full);
		free(full);
		if (res < 0) {
			return res;
		}
next:
		free(basename_dup);
		free(dirname_dup);
		res = archive_read_next_header(archive, &entry);
	}
	if (res == ARCHIVE_FATAL) {
		fprintf(stderr, "fatal: %s\n", archive_error_string(archive));
		return -archive_errno(archive);
	}
	return 0;
}

static int dump_layer(guestfs_h *guestfs, struct archive *archive) {
	struct archive_entry *entry;
	int res = archive_read_next_header(archive, &entry);
	while (res != ARCHIVE_EOF && res != ARCHIVE_FATAL) {
		if (res == ARCHIVE_WARN) {
			fprintf(stderr, "warning: %s: %s\n",
				archive_entry_pathname(entry),
				archive_error_string(archive));
		} else if (res == ARCHIVE_RETRY) {
			fprintf(stderr, "warning: %s: %s, retry\n",
				archive_entry_pathname(entry),
				archive_error_string(archive));
			goto next;
		}
		const char *path = archive_entry_pathname(entry);
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

		const char *hardlink = archive_entry_hardlink(entry);
		if (hardlink) {
			char *abs_target = calloc(2 + strlen(hardlink),
				sizeof(char));
			abs_target[0] = '/';
			strcpy(&abs_target[1], hardlink);
			int res = guestfs_ln(guestfs, abs_target, abs_path);
			if (res < 0) {
				return res;
			}
			free(abs_target);
			free(abs_path);
			goto next;
		}

		const struct stat *stat = archive_entry_stat(entry);

		if ((stat->st_mode & S_IFMT) != S_IFDIR) {
			// overwrites
			assert(guestfs_rm_rf(guestfs, abs_path) >= 0);
		} else {
			// try to remove non-dir if exists
			guestfs_rm_f(guestfs, abs_path);
		}

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
			if (guestfs_is_dir(guestfs, abs_path)) {
				res = guestfs_chmod(guestfs, stat->st_mode,
					abs_path);
			} else {
				res = guestfs_mkdir_mode(guestfs, abs_path,
					stat->st_mode);
			}
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
			fprintf(stderr, "dump_layer: unrecognized file type at %s\n", path);
			goto next;
		}

		if (res < 0) {
			return res;
		}
		res = set_attr(guestfs, abs_path, entry, stat);
		if (res < 0) {
			return res;
		}
		free(abs_path);
next:
		res = archive_read_next_header(archive, &entry);
	}
	if (res == ARCHIVE_FATAL) {
		fprintf(stderr, "fatal: %s\n", archive_error_string(archive));
		return -archive_errno(archive);
	}
	return 0;
}

static int append_quote_escaped_string(guestfs_h *guestfs, const char *path,
		const char *string) {
	int len = strlen(string), k = 0, res = 0;
	for (int j = 0; j < len; j++) {
		if (string[j] == '\'') {
			int part_len = j - k;
			if (part_len) {
				res = guestfs_write_append(guestfs, path,
					&string[k], part_len);
				if (res < 0) {
					return res;
				}
			}
			res = guestfs_write_append(guestfs,
				path, "\\'", 2);
			if (res < 0) {
				return res;
			}
			k = j + 1;
		}
	}
	int part_len = len - k;
	if (part_len) {
		res = guestfs_write_append(guestfs, path, &string[k], part_len);
		if (res < 0) {
			return res;
		}
	}
	return 0;
}

static int generate_init_script(guestfs_h *guestfs,
		struct cvirt_oci_r_config *config) {
	const char pre_env[] =
		C2V_BUSYBOX " umount -r /.old_root\n";
	int res = guestfs_write_append(guestfs, C2V_INIT, pre_env,
		strlen(pre_env));
	if (res < 0) {
		return res;
	}

	int env_count = cvirt_oci_r_config_get_env_length(config);
	for (int i = 0; i < env_count; i++) {
		const char export_pre[] = "export '";
		res = guestfs_write_append(guestfs, C2V_INIT,
			export_pre, strlen(export_pre));
		if (res < 0) {
			return res;
		}
		const char *env = cvirt_oci_r_config_get_env(config, i);
		res = append_quote_escaped_string(guestfs, C2V_INIT, env);
		if (res < 0) {
			return res;
		}
		const char export_post[] =
			"'\n";
		res = guestfs_write_append(guestfs, C2V_INIT,
			export_post, strlen(export_post));
		if (res < 0) {
			return res;
		}
	}

	const char *workdir = cvirt_oci_r_config_get_working_dir(config);
	if (workdir) {
		const char workdir_pre[] = "\ncd '";
		res = guestfs_write_append(guestfs, C2V_INIT,
			workdir_pre, strlen(workdir_pre));
		if (res < 0) {
			return res;
		}
		res = append_quote_escaped_string(guestfs, C2V_INIT, workdir);
		if (res < 0) {
			return res;
		}
		const char workdir_post[] =
			"'\n";
		res = guestfs_write_append(guestfs, C2V_INIT,
			workdir_post, strlen(workdir_post));
		if (res < 0) {
			return res;
		}
	}

	const char pre_cmd[] = "\nexec ";
	res = guestfs_write_append(guestfs, C2V_INIT,
		pre_cmd, strlen(pre_cmd));
	if (res < 0) {
		return res;
	}

	const char *user = cvirt_oci_r_config_get_user(config);
	if (user) {
		const char user_pre[] = " /.c2v/setuidgid '";
		res = guestfs_write_append(guestfs, C2V_INIT,
			user_pre, strlen(user_pre));
		if (res < 0) {
			return res;
		}
		res = append_quote_escaped_string(guestfs, C2V_INIT, user);
		const char user_post[] = "' ";
		res = guestfs_write_append(guestfs, C2V_INIT,
			user_post, strlen(user_post));
		if (res < 0) {
			return res;
		}
	}

	int entrypoint_len = cvirt_oci_r_config_get_entrypoint_length(config),
		cmd_len = cvirt_oci_r_config_get_cmd_length(config);
	if (!entrypoint_len && !cmd_len) {
		const char default_cmd[] = "/sbin/init\n";
		res = guestfs_write_append(guestfs, C2V_INIT,
			default_cmd, strlen(default_cmd));
		if (res < 0) {
			return res;
		}
	} else {
		for (int i = 0; i < entrypoint_len; i++) {
			const char pre[] = "'";
			res = guestfs_write_append(guestfs, C2V_INIT,
				pre, strlen(pre));
			if (res < 0) {
				return res;
			}
			const char *part = cvirt_oci_r_config_get_entrypoint_part(config, i);
			res = append_quote_escaped_string(guestfs, C2V_INIT, part);
			if (res < 0) {
				return res;
			}
			const char post[] =
				"' ";
			res = guestfs_write_append(guestfs, C2V_INIT,
				post, strlen(post));
			if (res < 0) {
				return res;
			}
		}
		for (int i = 0; i < cmd_len; i++) {
			const char pre[] = "'";
			res = guestfs_write_append(guestfs, C2V_INIT,
				pre, strlen(pre));
			if (res < 0) {
				return res;
			}
			const char *part = cvirt_oci_r_config_get_cmd_part(config, i);
			res = append_quote_escaped_string(guestfs, C2V_INIT, part);
			if (res < 0) {
				return res;
			}
			const char post[] =
				"' ";
			res = guestfs_write_append(guestfs, C2V_INIT,
				post, strlen(post));
			if (res < 0) {
				return res;
			}
		}
	}

	res = guestfs_chmod(guestfs, 0400, C2V_INIT);
	return 0;
}

static int block_sz = 4096;

static size_t estimate_disk_usage(const struct cvirt_io_entry *entry) {
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

static void restore_mtime(guestfs_h *guestfs, struct cvirt_io_entry *entry,
		const char *path) {
	for (int i = 0; i < entry->inode->children_len; i++) {
		if (!S_ISDIR(entry->inode->children[i].inode->stat.st_mode)) {
			continue;
		}
		char mpath[strlen(path) + strlen(entry->inode->children[i].name) + 2];
		strcpy(mpath, path);
		strcat(mpath, entry->inode->children[i].name);
		strcat(mpath, "/");
		restore_mtime(guestfs, &entry->inode->children[i], mpath);
	}

	guestfs_utimens(guestfs, path, entry->inode->stat.st_atim.tv_sec,
		entry->inode->stat.st_atim.tv_nsec, entry->inode->stat.st_mtim.tv_sec,
		entry->inode->stat.st_mtim.tv_nsec);
}

int main(int argc, char *argv[]) {
	if (argc != 3) {
		exit(EXIT_FAILURE);
	}

	int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	assert(fd != -1);
	struct cvirt_oci_r_index *index = cvirt_oci_r_index_from_archive(fd);
	const char *manifest_digest = cvirt_oci_r_index_get_native_manifest_digest(index);
	struct cvirt_oci_r_manifest *manifest = cvirt_oci_r_manifest_from_archive_blob(fd, manifest_digest);
	const char *config_digest = cvirt_oci_r_manifest_get_config_digest(manifest);
	int len = cvirt_oci_r_manifest_get_layers_length(manifest);

	struct cvirt_io_entry *layer_trees[len];

	struct cvirt_oci_r_layer *layer = cvirt_oci_r_layer_from_archive_blob(
		fd, cvirt_oci_r_manifest_get_layer_digest(manifest, 0),
		cvirt_oci_r_manifest_get_layer_compression(manifest, 0));
	layer_trees[0] = cvirt_io_tree_from_oci_layer(layer, 0);
	size_t needed = estimate_disk_usage(layer_trees[0]);
	cvirt_oci_r_layer_destroy(layer);
	for (int i = 1; i < len; i++) {
		const char *layer_digest = cvirt_oci_r_manifest_get_layer_digest(manifest, i);
		struct cvirt_oci_r_layer *layer =
			cvirt_oci_r_layer_from_archive_blob(fd, layer_digest,
			cvirt_oci_r_manifest_get_layer_compression(manifest, i));
		layer_trees[i] = cvirt_io_tree_from_oci_layer(layer, 0);
		needed += estimate_disk_usage(layer_trees[i]);
		cvirt_oci_r_layer_destroy(layer);
	}

	guestfs_h *guestfs = create_qcow2_btrfs_image(argv[2], needed * 2);
	if (!guestfs) {
		exit(EXIT_FAILURE);
	}
	assert(guestfs_umask(guestfs, 0) >= 0);
	
	assert(guestfs_mkdir_mode(guestfs, C2V_DIR, 0500) >= 0);
	assert(guestfs_mkdir_mode(guestfs, C2V_LAYERS, 0500) >= 0);
	assert(guestfs_btrfs_subvolume_snapshot_opts(guestfs, "/",
		C2V_LAYERS "/base",
		GUESTFS_BTRFS_SUBVOLUME_SNAPSHOT_OPTS_RO, 1, -1) >= 0);

	for (int i = 0; i < len; i++) {
		const char *layer_digest = cvirt_oci_r_manifest_get_layer_digest(manifest, i);
		struct cvirt_oci_r_layer *layer =
			cvirt_oci_r_layer_from_archive_blob(fd, layer_digest,
			cvirt_oci_r_manifest_get_layer_compression(manifest, i));

		// first pass: whiteouts
		struct archive *archive = cvirt_oci_r_layer_get_libarchive(layer);
		apply_whiteouts(guestfs, archive);

		// second pass: data
		cvirt_oci_r_layer_rewind(layer);
		archive = cvirt_oci_r_layer_get_libarchive(layer);
		dump_layer(guestfs, archive);
		cvirt_oci_r_layer_destroy(layer);

		restore_mtime(guestfs, layer_trees[i], "/");

		cvirt_io_tree_destroy(layer_trees[i]);

		// snapshot after each layer
		char *snapshot_dest = calloc(strlen(layer_digest) + 14, sizeof(char));
		assert(snapshot_dest);
		strcpy(snapshot_dest, C2V_LAYERS "/");
		strcat(snapshot_dest, layer_digest);
		assert(guestfs_btrfs_subvolume_snapshot_opts(guestfs, "/",
			snapshot_dest,
			GUESTFS_BTRFS_SUBVOLUME_SNAPSHOT_OPTS_RO, 1, -1) >= 0);
		free(snapshot_dest);
	}
	struct cvirt_oci_r_config *config =
		cvirt_oci_r_config_from_archive_blob(fd, config_digest);
	generate_init_script(guestfs, config);
	cvirt_oci_r_config_destroy(config);
	cvirt_oci_r_manifest_destroy(manifest);
	cvirt_oci_r_index_destroy(index);
	close(fd);

	guestfs_umount_all(guestfs);
	guestfs_shutdown(guestfs);
	guestfs_close(guestfs);
	return 0;
}
