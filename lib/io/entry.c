#include <convirter/io/entry.h>
#include <convirter/io/xattr.h>
#include <convirter/oci-r/layer.h>

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <archive.h>
#include <archive_entry.h>

#include "hex.h"
#include "io/entry.h"

static void copy_stat_from_guestfs_statns(struct cvirt_io_entry *entry,
		struct guestfs_statns *statns) {
	entry->stat.st_dev = statns->st_dev;
	entry->stat.st_ino = statns->st_ino;
	entry->stat.st_mode = statns->st_mode;
	entry->stat.st_nlink = statns->st_nlink;
	entry->stat.st_uid = statns->st_uid;
	entry->stat.st_gid = statns->st_gid;
	entry->stat.st_rdev = statns->st_rdev;
	entry->stat.st_size = statns->st_size;
	entry->stat.st_blksize = statns->st_blksize;
	entry->stat.st_blocks = statns->st_blocks;
	entry->stat.st_atim.tv_sec = statns->st_atime_sec;
	entry->stat.st_atim.tv_nsec = statns->st_atime_nsec;
	entry->stat.st_mtim.tv_sec = statns->st_mtime_sec;
	entry->stat.st_mtim.tv_nsec = statns->st_mtime_nsec;
	entry->stat.st_ctim.tv_sec = statns->st_ctime_sec;
	entry->stat.st_ctim.tv_nsec = statns->st_ctime_nsec;
}

static void set_xattrs_from_guestfs_xattr_array(struct cvirt_io_entry *entry,
		struct guestfs_xattr *xattrs, int count) {
	if (!count) {
		return;
	}
	entry->xattrs = calloc(count, sizeof(struct cvirt_io_xattr));
	assert(entry->xattrs);
	entry->xattrs_capacity = count;
	entry->xattrs_len = count;
	for (int i = 0; i < count; i++) {
		entry->xattrs[i].name = strdup(xattrs[i].attrname);
		assert(entry->xattrs[i].name);
		entry->xattrs[i].value = calloc(xattrs[i].attrval_len, sizeof(uint8_t));
		assert(entry->xattrs[i].value);
		memcpy(entry->xattrs[i].value, xattrs[i].attrval,
			xattrs[i].attrval_len);
		entry->xattrs[i].len = xattrs[i].attrval_len;
	}
}

static void guestfs_dir_fill_children(struct cvirt_io_entry *entry,
		guestfs_h *guestfs, const char *path, uint32_t flags) {
	char **ls = guestfs_ls(guestfs, path);
	if (!ls) {
		return;
	} else if (!ls[0]) {
		free(ls);
		return;
	}
	int i = 0;
	int max_length = 0;
	while (ls[i]) {
		int l = strlen(ls[i]);
		max_length = (max_length > l) ? max_length : l;
		i++;
	}
	entry->children = calloc(i, sizeof(struct cvirt_io_entry));
	assert(entry->children);
	entry->children_capacity = i;
	entry->children_len = i;
	struct guestfs_statns_list *stats = guestfs_lstatnslist(guestfs, path, ls);
	assert(stats);
	assert(stats->len == entry->children_len);

	struct guestfs_xattr_list *xattrs = guestfs_lxattrlist(guestfs, path, ls);
	assert(xattrs);
	int xattrs_idx = 0;

	int common_len = strlen(path);
	char *abs_path = calloc(common_len + 1 + max_length + 1, sizeof(char));
	strcpy(abs_path, path);
	strcat(abs_path, "/");
	assert(abs_path);
	for (int i = 0; i < entry->children_len; i++) {
		entry->children[i].name = ls[i];
		copy_stat_from_guestfs_statns(&entry->children[i], &stats->val[i]);

		strcpy(&abs_path[common_len + 1], ls[i]);

		assert(xattrs_idx < xattrs->len);
		int l = atoi(xattrs->val[xattrs_idx].attrval);
		xattrs_idx++;
		set_xattrs_from_guestfs_xattr_array(&entry->children[i], &xattrs->val[xattrs_idx], l);
		xattrs_idx += l;

		if (S_ISDIR(entry->children[i].stat.st_mode)) {
			guestfs_dir_fill_children(&entry->children[i], guestfs, abs_path, flags);
		} else if (S_ISLNK(entry->children[i].stat.st_mode)) {
			char *link = guestfs_readlink(guestfs, abs_path);
			assert(link);
			entry->children[i].target = link;
		} else if ((flags & CVIRT_IO_TREE_CHECKSUM) &&
				S_ISREG(entry->children[i].stat.st_mode)) {
			char *buf = guestfs_checksum(guestfs, "sha256", abs_path);
			hex_to_bin(entry->children[i].sha256sum, buf, 32);
			free(buf);
		}
	}
	guestfs_free_statns_list(stats);
	guestfs_free_xattr_list(xattrs);
	free(abs_path);
	free(ls);
}

struct cvirt_io_entry *cvirt_io_tree_from_guestfs(guestfs_h *guestfs, uint32_t flags) {
	struct cvirt_io_entry *result = calloc(1, sizeof(struct cvirt_io_entry));
	if (!result) {
		return NULL;
	}

	result->name = strdup("/");
	assert(result->name);

	struct guestfs_statns *stat = guestfs_lstatns(guestfs, "/");
	assert(stat);
	copy_stat_from_guestfs_statns(result, stat);
	guestfs_free_statns(stat);

	struct guestfs_xattr_list *xattrs = guestfs_lgetxattrs(guestfs, "/");
	assert(xattrs);
	set_xattrs_from_guestfs_xattr_array(result, xattrs->val, xattrs->len);
	guestfs_free_xattr_list(xattrs);

	guestfs_dir_fill_children(result, guestfs, "/", flags);

	return result;
}

// normalize /foo ./foo foo foo/ ... -> foo
// TODO also sanitize?
static char *normalize_tar_entry_name(const char *entry) {
	assert(entry);
	char *res;
	if (entry[0] == '/') {
		res = strdup(&entry[1]);
	} else if (entry[0] == '.' && entry[1] == '/') {
		res = strdup(&entry[2]);
	} else {
		res = strdup(entry);
	}
	assert(res);
	int l = strlen(res);
	if (res[l - 1] == '/') {
		res[l - 1] = '\0';
	}
	return res;
}

static int path_first_part_len(const char *path) {
	int c = 0;
	while (*path && *path != '/') {
		path++;
		c++;
	}
	return c;
}

static struct cvirt_io_entry *allocate_child(struct cvirt_io_entry *entry,
		const char *name, int name_len) {
	if (entry->children_len < entry->children_capacity) {
		goto prepare_entry;
	}

	const unsigned int sz = entry->children ? entry->children_capacity * 2 : 8;
	entry->children = realloc(entry->children, sz * sizeof(struct cvirt_io_entry));
	memset(&entry->children[entry->children_capacity], 0, (sz - entry->children_capacity) * sizeof(struct cvirt_io_entry));
	entry->children_capacity = sz;

prepare_entry:
	entry->children[entry->children_len].name = strndup(name, name_len);
	assert(entry->children[entry->children_len].name);
	return &entry->children[entry->children_len++];
}

static struct cvirt_io_entry *find_entry(struct cvirt_io_entry *entry,
		const char *name) {
	int first_part_len = path_first_part_len(name);
	int name_len = strlen(name);
	bool last = first_part_len == name_len;
	if (!first_part_len) { // target should be root
		assert(entry->name[0] == '/');
		return entry;
	}

	for (int i = 0; i < entry->children_len; i++) {
		if (strlen(entry->children[i].name) == first_part_len &&
				!strncmp(entry->children[i].name, name, first_part_len)) {
			if (!S_ISDIR(entry->stat.st_mode)) {
				if (last) {
					return &entry->children[i];
				} else {
					// TODO try to follow symlinks?
					assert("Parent is not a directory" == NULL);
				}
			}
			struct cvirt_io_entry *res = find_entry(
				&entry->children[i], &name[first_part_len + 1]);
			if (res) {
				return res;
			}
		}
	}

	// new entry
	assert(last && "Missing parent directory entries.");
	return allocate_child(entry, name, first_part_len);
}

static void copy_stat(struct stat *dest, const struct stat *src) {
	dest->st_dev = src->st_dev;
	dest->st_ino = src->st_ino;
	dest->st_mode = src->st_mode;
	dest->st_nlink = src->st_nlink;
	dest->st_uid = src->st_uid;
	dest->st_gid = src->st_gid;
	dest->st_rdev = src->st_rdev;
	dest->st_size = src->st_size;
	dest->st_blksize = src->st_blksize;
	dest->st_blocks = src->st_blocks;
	dest->st_atim.tv_sec = src->st_atim.tv_sec;
	dest->st_atim.tv_nsec = src->st_atim.tv_nsec;
	dest->st_mtim.tv_sec = src->st_mtim.tv_sec;
	dest->st_mtim.tv_nsec = src->st_mtim.tv_nsec;
	dest->st_ctim.tv_sec = src->st_ctim.tv_sec;
	dest->st_ctim.tv_nsec = src->st_ctim.tv_nsec;
}

static void set_xattr_from_libarchive(struct cvirt_io_entry *entry,
		struct archive_entry *archive_entry) {
	int count = archive_entry_xattr_count(archive_entry);
	if (!count) {
		return;
	}

	entry->xattrs = calloc(count, sizeof(struct cvirt_io_xattr));
	assert(entry->xattrs);
	entry->xattrs_len = count;
	entry->xattrs_capacity = count;
	archive_entry_xattr_reset(archive_entry);

	const char *name;
	const void *val;
	size_t sz;

	for (int i = 0; i < count; i++) {
		archive_entry_xattr_next(archive_entry, &name, &val, &sz);
		entry->xattrs[i].name = strdup(name);
		assert(entry->xattrs[i].name);
		entry->xattrs[i].value = calloc(sz, sizeof(uint8_t));
		assert(entry->xattrs[i].value);
		memcpy(entry->xattrs[i].value, val, sz);
		entry->xattrs[i].len = sz;
	}
}

static void checksum_from_archive(uint8_t *dest, struct archive *archive,
		size_t sz, struct io_entry_oci_checksum_ctx *ctx) {
	while (sz > IO_ENTRY_OCI_BUF_LEN) {
		la_ssize_t read = archive_read_data(archive, ctx->buffer, IO_ENTRY_OCI_BUF_LEN);
		gcry_md_write(ctx->gcrypt_handle, ctx->buffer, read);
		sz -= read;
	}
	while (sz > 0) {
		la_ssize_t read = archive_read_data(archive, ctx->buffer, sz);
		gcry_md_write(ctx->gcrypt_handle, ctx->buffer, read);
		sz -= read;
	}
	memcpy(dest, gcry_md_read(ctx->gcrypt_handle, 0), 32);
	gcry_md_reset(ctx->gcrypt_handle);
}

struct cvirt_io_entry *cvirt_io_tree_from_oci_layer(struct cvirt_oci_r_layer *layer, uint32_t flags) {
	struct cvirt_io_entry *result = calloc(1, sizeof(struct cvirt_io_entry));
	if (!result) {
		return NULL;
	}

	struct io_entry_oci_checksum_ctx *checksum_ctx = NULL;
	if (flags & CVIRT_IO_TREE_CHECKSUM) {
		checksum_ctx = calloc(1, sizeof(struct io_entry_oci_checksum_ctx));
		assert(checksum_ctx);
		gcry_md_open(&checksum_ctx->gcrypt_handle, GCRY_MD_SHA256, 0);
	}

	result->name = strdup("/");
	assert(result->name);

	result->stat.st_mode = S_IFDIR | 0755;

	struct archive *archive = cvirt_oci_r_layer_get_libarchive(layer);
	struct archive_entry *archive_entry;
	int res;
	while ((res = archive_read_next_header(archive, &archive_entry)) != ARCHIVE_EOF
			&& res != ARCHIVE_FATAL) {
		char *path = normalize_tar_entry_name(
			archive_entry_pathname(archive_entry));
		struct cvirt_io_entry *entry = find_entry(result, path);
		free(path);

		const struct stat *stat = archive_entry_stat(archive_entry);
		copy_stat(&entry->stat, stat);

		set_xattr_from_libarchive(entry, archive_entry);

		if (S_ISLNK(entry->stat.st_mode)) {
			char *link = strdup(archive_entry_symlink(archive_entry));
			assert(link);
			entry->target = link;
		} else if ((flags & CVIRT_IO_TREE_CHECKSUM) &&
				S_ISREG(entry->stat.st_mode)) {
			checksum_from_archive(entry->sha256sum, archive,
				archive_entry_size(archive_entry), checksum_ctx);
		}
	}
	free(checksum_ctx);
	if (res == ARCHIVE_FATAL) {
		cvirt_io_tree_destroy(result);
		return NULL;
	}

	return result;
}

static void entry_cleanup(struct cvirt_io_entry *entry) {
	free(entry->name);
	for (int i = 0; i < entry->xattrs_len; i++) {
		free(entry->xattrs[i].name);
		free(entry->xattrs[i].value);
	}
	free(entry->xattrs);

	if (S_ISDIR(entry->stat.st_mode)) {
		for (int i = 0; i < entry->children_len; i++) {
			entry_cleanup(&entry->children[i]);
		}
		free(entry->children);
	} else if (S_ISLNK(entry->stat.st_mode)) {
		free(entry->target);
	}
}

void cvirt_io_tree_destroy(struct cvirt_io_entry *entry) {
	if (!entry) {
		return;
	}

	entry_cleanup(entry);

	free(entry);
}
