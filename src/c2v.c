#include <archive.h>
#include <archive_entry.h>
#include <convirter/oci-r/config.h>
#include <convirter/oci-r/index.h>
#include <convirter/oci-r/layer.h>
#include <convirter/oci-r/manifest.h>
#include <guestfs.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysmacros.h>

static const int bufsz = 4000 * 1024;

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
		const struct stat *stat = archive_entry_stat(entry);
		const char *path = archive_entry_pathname(entry);
		char *abs_path = calloc(2 + strlen(path), sizeof(char));
		const char *link;
		abs_path[0] = '/';
		strcat(abs_path, path);
		switch (stat->st_mode & S_IFMT) {
		case S_IFLNK:
			res = guestfs_ln_sf(guestfs, archive_entry_symlink(entry), abs_path);
			break;
		case S_IFREG:
			guestfs_rm_f(guestfs, abs_path);
			res = guestfs_mknod(guestfs, stat->st_mode,
				major(stat->st_rdev), minor(stat->st_rdev),
				abs_path);
			if (res < 0) {
				break;
			}
			res = dump_file(guestfs, archive, abs_path, stat);
			break;
		case S_IFDIR:
			guestfs_mkdir_mode(guestfs, abs_path, stat->st_mode);
			break;
		case S_IFSOCK:
		case S_IFBLK:
		case S_IFCHR:
		case S_IFIFO:
			guestfs_rm_f(guestfs, abs_path);
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
				res = guestfs_ln_f(guestfs, abs_target, abs_path);
				free(abs_target);
			} else {
				fprintf(stderr, "dump_layer: unrecognized file type at %s\n", path);
			}
			goto next;
		}
		if (res < 0) {
			fputs(guestfs_last_error(guestfs), stderr);
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
	}
	return 0;
}

static const char c2v_init_path[] = "/.c2v/init";
static const char c2v_dir_path[] = "/.c2v";

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

#define C2V_BUSYBOX "/.c2v/busybox"

/*
 * TODO: templating
 */
static int generate_init_script(guestfs_h *guestfs,
		struct cvirt_oci_r_config *config) {
	guestfs_mkdir_p(guestfs, c2v_dir_path);
	guestfs_rm_rf(guestfs, c2v_init_path);
	const char pre_env[] =
		C2V_BUSYBOX " umount -r /.old_root\n";
	int res = guestfs_write_append(guestfs, c2v_init_path, pre_env,
		strlen(pre_env));
	if (res < 0) {
		return res;
	}

	int env_count = cvirt_oci_r_config_get_env_length(config);
	for (int i = 0; i < env_count; i++) {
		const char export_pre[] = "export '";
		res = guestfs_write_append(guestfs, c2v_init_path,
			export_pre, strlen(export_pre));
		if (res < 0) {
			return res;
		}
		const char *env = cvirt_oci_r_config_get_env(config, i);
		res = append_quote_escaped_string(guestfs, c2v_init_path, env);
		if (res < 0) {
			return res;
		}
		const char export_post[] =
			"'\n";
		res = guestfs_write_append(guestfs, c2v_init_path,
			export_post, strlen(export_post));
		if (res < 0) {
			return res;
		}
	}

	const char *workdir = cvirt_oci_r_config_get_working_dir(config);
	if (workdir) {
		const char workdir_pre[] = "\ncd '";
		res = guestfs_write_append(guestfs, c2v_init_path,
			workdir_pre, strlen(workdir_pre));
		if (res < 0) {
			return res;
		}
		res = append_quote_escaped_string(guestfs, c2v_init_path, workdir);
		if (res < 0) {
			return res;
		}
		const char workdir_post[] =
			"'\n";
		res = guestfs_write_append(guestfs, c2v_init_path,
			workdir_post, strlen(workdir_post));
		if (res < 0) {
			return res;
		}
	}

	const char pre_cmd[] = "\nexec ";
	res = guestfs_write_append(guestfs, c2v_init_path,
		pre_cmd, strlen(pre_cmd));
	if (res < 0) {
		return res;
	}

	// TODO: setuidgid does not support supp. groups
	const char *user = cvirt_oci_r_config_get_user(config);
	if (user) {
		const char user_pre[] = " /.c2v/setuidgid '";
		res = guestfs_write_append(guestfs, c2v_init_path,
			user_pre, strlen(user_pre));
		if (res < 0) {
			return res;
		}
		res = append_quote_escaped_string(guestfs, c2v_init_path, user);
		const char user_post[] = "' ";
		res = guestfs_write_append(guestfs, c2v_init_path,
			user_post, strlen(user_post));
		if (res < 0) {
			return res;
		}
	}

	int entrypoint_len = cvirt_oci_r_config_get_entrypoint_length(config),
		cmd_len = cvirt_oci_r_config_get_cmd_length(config);
	if (!entrypoint_len && !cmd_len) {
		const char default_cmd[] = "/sbin/init\n";
		res = guestfs_write_append(guestfs, c2v_init_path,
			default_cmd, strlen(default_cmd));
		if (res < 0) {
			return res;
		}
	} else {
		for (int i = 0; i < entrypoint_len; i++) {
			const char pre[] = "'";
			res = guestfs_write_append(guestfs, c2v_init_path,
				pre, strlen(pre));
			if (res < 0) {
				return res;
			}
			const char *part = cvirt_oci_r_config_get_entrypoint_part(config, i);
			res = append_quote_escaped_string(guestfs, c2v_init_path, part);
			if (res < 0) {
				return res;
			}
			const char post[] =
				"' ";
			res = guestfs_write_append(guestfs, c2v_init_path,
				post, strlen(post));
			if (res < 0) {
				return res;
			}
		}
		for (int i = 0; i < cmd_len; i++) {
			const char pre[] = "'";
			res = guestfs_write_append(guestfs, c2v_init_path,
				pre, strlen(pre));
			if (res < 0) {
				return res;
			}
			const char *part = cvirt_oci_r_config_get_cmd_part(config, i);
			res = append_quote_escaped_string(guestfs, c2v_init_path, part);
			if (res < 0) {
				return res;
			}
			const char post[] =
				"' ";
			res = guestfs_write_append(guestfs, c2v_init_path,
				post, strlen(post));
			if (res < 0) {
				return res;
			}
		}
	}

	res = guestfs_chmod(guestfs, 0400, c2v_init_path);
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
	struct cvirt_oci_r_config *config =
		cvirt_oci_r_config_from_archive_blob(argv[1], config_digest);
	generate_init_script(guestfs, config);
	cvirt_oci_r_config_destroy(config);
	cvirt_oci_r_manifest_destroy(manifest);
	cvirt_oci_r_index_destroy(index);

	guestfs_umount_all(guestfs);
	guestfs_shutdown(guestfs);
	guestfs_close(guestfs);
	return 0;
}
