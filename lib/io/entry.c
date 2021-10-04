#include <convirter/io/entry.h>
#include <convirter/io/xattr.h>
#include <convirter/oci-r/layer.h>

#include <assert.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <archive.h>
#include <archive_entry.h>

#include "hex.h"
#include "io/entry.h"
#include "xmem.h"

static void copy_stat_from_guestfs_statns(struct cvirt_io_inode *inode,
		struct guestfs_statns *statns) {
	inode->stat.st_dev = statns->st_dev;
	inode->stat.st_ino = statns->st_ino;
	inode->stat.st_mode = statns->st_mode;
	inode->stat.st_nlink = 1;
	inode->stat.st_uid = statns->st_uid;
	inode->stat.st_gid = statns->st_gid;
	inode->stat.st_rdev = statns->st_rdev;
	inode->stat.st_size = statns->st_size;
	inode->stat.st_blksize = statns->st_blksize;
	inode->stat.st_blocks = statns->st_blocks;
	inode->stat.st_atim.tv_sec = statns->st_atime_sec;
	inode->stat.st_atim.tv_nsec = statns->st_atime_nsec;
	inode->stat.st_mtim.tv_sec = statns->st_mtime_sec;
	inode->stat.st_mtim.tv_nsec = statns->st_mtime_nsec;
	inode->stat.st_ctim.tv_sec = statns->st_ctime_sec;
	inode->stat.st_ctim.tv_nsec = statns->st_ctime_nsec;
}

static void set_xattrs_from_guestfs_xattr_array(struct cvirt_io_inode *inode,
		struct guestfs_xattr *xattrs, int count) {
	if (!count) {
		return;
	}
	inode->xattrs = calloc(count, sizeof(struct cvirt_io_xattr));
	assert(inode->xattrs);
	inode->xattrs_capacity = count;
	inode->xattrs_len = count;
	for (int i = 0; i < count; i++) {
		inode->xattrs[i].name = strdup(xattrs[i].attrname);
		assert(inode->xattrs[i].name);
		inode->xattrs[i].value = calloc(xattrs[i].attrval_len, sizeof(uint8_t));
		assert(inode->xattrs[i].value);
		memcpy(inode->xattrs[i].value, xattrs[i].attrval,
			xattrs[i].attrval_len);
		inode->xattrs[i].len = xattrs[i].attrval_len;
	}
}

static void checksum_from_guestfs(uint8_t *dest, guestfs_h *guestfs,
		const char *path, size_t sz,
		struct io_entry_guestfs_checksum_ctx *ctx) {
	size_t offset = 0, read = 0;
	while (sz > 0) {
		char *buf = guestfs_pread(guestfs, path,
			sz > IO_ENTRY_GUESTFS_BUF_LEN ?
			IO_ENTRY_GUESTFS_BUF_LEN : sz, offset, &read);
		gcry_md_write(ctx->gcrypt_handle, buf, read);
		free(buf);
		sz -= read;
		offset += read;
	}
	memcpy(dest, gcry_md_read(ctx->gcrypt_handle, 0), 32);
	gcry_md_reset(ctx->gcrypt_handle);
}

static void guestfs_dir_fill_children(struct cvirt_io_entry *entry,
		guestfs_h *guestfs, const char *path, uint32_t flags,
		struct io_entry_guestfs_checksum_ctx *checksum_ctx) {
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
	struct cvirt_io_inode *inode = entry->inode;
	inode->children = calloc(i, sizeof(struct cvirt_io_entry));
	assert(inode->children);
	inode->children_capacity = i;
	inode->children_len = i;
	struct guestfs_statns_list *stats = guestfs_lstatnslist(guestfs, path, ls);
	assert(stats);
	assert(stats->len == inode->children_len);

	struct guestfs_xattr_list *xattrs = guestfs_lxattrlist(guestfs, path, ls);
	assert(xattrs);
	int xattrs_idx = 0;

	int common_len = strlen(path);
	char *abs_path = calloc(common_len + 1 + max_length + 1, sizeof(char));
	strcpy(abs_path, path);
	strcat(abs_path, "/");
	assert(abs_path);
	for (int i = 0; i < inode->children_len; i++) {
		inode->children[i].name = ls[i];
		inode->children[i].inode = cvirt_xcalloc(1, sizeof(struct cvirt_io_inode));
		copy_stat_from_guestfs_statns(inode->children[i].inode, &stats->val[i]);

		strcpy(&abs_path[common_len + 1], ls[i]);

		assert(xattrs_idx < xattrs->len);
		int l = atoi(xattrs->val[xattrs_idx].attrval);
		xattrs_idx++;
		set_xattrs_from_guestfs_xattr_array(inode->children[i].inode, &xattrs->val[xattrs_idx], l);
		xattrs_idx += l;

		if (S_ISDIR(inode->children[i].inode->stat.st_mode)) {
			guestfs_dir_fill_children(&inode->children[i], guestfs,
				abs_path, flags, checksum_ctx);
		} else if (S_ISLNK(inode->children[i].inode->stat.st_mode)) {
			char *link = guestfs_readlink(guestfs, abs_path);
			assert(link);
			inode->children[i].inode->target = link;
		} else if ((flags & CVIRT_IO_TREE_CHECKSUM) &&
				S_ISREG(inode->children[i].inode->stat.st_mode)) {
			checksum_from_guestfs(inode->children[i].inode->sha256sum,
				guestfs, abs_path,
				inode->children[i].inode->stat.st_size, checksum_ctx);
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

	struct io_entry_guestfs_checksum_ctx *checksum_ctx = NULL;
	if (flags & CVIRT_IO_TREE_CHECKSUM) {
		checksum_ctx = calloc(1, sizeof(struct io_entry_guestfs_checksum_ctx));
		assert(checksum_ctx);
		gcry_md_open(&checksum_ctx->gcrypt_handle, GCRY_MD_SHA256, 0);
	}

	result->name = strdup("/");
	assert(result->name);
	result->inode = cvirt_xcalloc(1, sizeof(struct cvirt_io_inode));

	struct guestfs_statns *stat = guestfs_lstatns(guestfs, "/");
	assert(stat);
	copy_stat_from_guestfs_statns(result->inode, stat);
	guestfs_free_statns(stat);

	struct guestfs_xattr_list *xattrs = guestfs_lgetxattrs(guestfs, "/");
	assert(xattrs);
	set_xattrs_from_guestfs_xattr_array(result->inode, xattrs->val, xattrs->len);
	guestfs_free_xattr_list(xattrs);

	guestfs_dir_fill_children(result, guestfs, "/", flags, checksum_ctx);

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

static struct cvirt_io_entry *allocate_child(struct cvirt_io_inode *inode,
		const char *name, int name_len) {
	if (inode->children_len < inode->children_capacity) {
		goto prepare_entry;
	}

	const unsigned int sz = inode->children ? inode->children_capacity * 2 : 8;
	inode->children = realloc(inode->children, sz * sizeof(struct cvirt_io_entry));
	memset(&inode->children[inode->children_capacity], 0, (sz - inode->children_capacity) * sizeof(struct cvirt_io_entry));
	inode->children_capacity = sz;

prepare_entry:
	inode->children[inode->children_len].name = strndup(name, name_len);
	inode->children[inode->children_len].inode = NULL;
	assert(inode->children[inode->children_len].name);
	return &inode->children[inode->children_len++];
}

static struct cvirt_io_entry *find_entry(struct cvirt_io_entry *entry,
		const char *name, bool create) {
	int first_part_len = path_first_part_len(name);
	int name_len = strlen(name);
	bool last = first_part_len == name_len;
	if (!first_part_len) { // target should be root
		assert(entry->name[0] == '/');
		return entry;
	}

	struct cvirt_io_inode *inode = entry->inode;
	for (int i = 0; i < inode->children_len; i++) {
		if (strlen(inode->children[i].name) == first_part_len &&
				!strncmp(inode->children[i].name, name, first_part_len)) {
			if (last) {
				return &inode->children[i];
			}
			if (!S_ISDIR(inode->stat.st_mode)) {
				// TODO try to follow symlinks?
				assert("Parent is not a directory" == NULL);
			}

			struct cvirt_io_entry *res = find_entry(
				&inode->children[i], &name[first_part_len + 1],
				create);
			if (res) {
				return res;
			}
		}
	}

	if (!create) {
		return NULL;
	}

	// new entry
	assert(last && "Missing parent directory entries.");
	return allocate_child(inode, name, first_part_len);
}

static void copy_stat(struct stat *dest, const struct stat *src) {
	dest->st_dev = src->st_dev;
	dest->st_ino = src->st_ino;
	dest->st_mode = src->st_mode;
	dest->st_nlink = 1;
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

static void set_xattr_from_libarchive(struct cvirt_io_inode *inode,
		struct archive_entry *archive_entry) {
	int count = archive_entry_xattr_count(archive_entry);
	if (!count) {
		return;
	}

	inode->xattrs = calloc(count, sizeof(struct cvirt_io_xattr));
	assert(inode->xattrs);
	inode->xattrs_len = count;
	inode->xattrs_capacity = count;
	archive_entry_xattr_reset(archive_entry);

	const char *name;
	const void *val;
	size_t sz;

	for (int i = 0; i < count; i++) {
		archive_entry_xattr_next(archive_entry, &name, &val, &sz);
		inode->xattrs[i].name = strdup(name);
		assert(inode->xattrs[i].name);
		inode->xattrs[i].value = calloc(sz, sizeof(uint8_t));
		assert(inode->xattrs[i].value);
		memcpy(inode->xattrs[i].value, val, sz);
		inode->xattrs[i].len = sz;
	}
}

static void checksum_from_archive(uint8_t *dest, struct archive *archive,
		size_t sz, struct io_entry_oci_checksum_ctx *ctx) {
	off_t last_pos = 0;
	const void *buf;
	size_t len;
	off_t offset;
	/*
	 * Since OCI spec discourage use of sparse entries, let's use
	 * archive_read_data_block directly instead of archive_read_data
	 */
	while (archive_read_data_block(archive, &buf, &len, &offset) != ARCHIVE_EOF) {
		while (last_pos < offset) {
			gcry_md_putc(ctx->gcrypt_handle, 0);
			last_pos++;
		}
		gcry_md_write(ctx->gcrypt_handle, buf, len);
		last_pos = offset + len;
	}
	while (last_pos < offset) {
		gcry_md_putc(ctx->gcrypt_handle, 0);
		last_pos++;
	}
	memcpy(dest, gcry_md_read(ctx->gcrypt_handle, 0), 32);
	gcry_md_reset(ctx->gcrypt_handle);
}

static int apply_layer_addition(struct cvirt_io_entry *root,
		struct cvirt_oci_r_layer *layer, uint32_t flags) {
	struct io_entry_oci_checksum_ctx *checksum_ctx = NULL;
	if (flags & CVIRT_IO_TREE_CHECKSUM) {
		checksum_ctx = calloc(1, sizeof(struct io_entry_oci_checksum_ctx));
		assert(checksum_ctx);
		gcry_md_open(&checksum_ctx->gcrypt_handle, GCRY_MD_SHA256, 0);
	}

	struct archive *archive = cvirt_oci_r_layer_get_libarchive(layer);
	struct archive_entry *archive_entry;
	int res;
	while ((res = archive_read_next_header(archive, &archive_entry)) != ARCHIVE_EOF
			&& res != ARCHIVE_FATAL) {
		const char *orig_name = archive_entry_pathname(archive_entry);
		char *basename_dup = cvirt_xstrdup(orig_name);
		if (!strncmp(basename(basename_dup), ".wh.", 4)) {
			// whiteouts
			free(basename_dup);
			continue;
		}
		free(basename_dup);

		char *path = normalize_tar_entry_name(orig_name);
		struct cvirt_io_entry *entry = find_entry(root, path, true);
		free(path);

		const char *hardlink = archive_entry_hardlink(archive_entry);
		if (hardlink) { // hardlink
			char *linkpath = normalize_tar_entry_name(hardlink);
			struct cvirt_io_entry *target = find_entry(root, linkpath, false);
			assert(target);
			free(linkpath);
			entry->inode = target->inode;
			entry->inode->stat.st_nlink++;
			continue;
		}

		if (!entry->inode) {
			entry->inode = cvirt_xcalloc(1, sizeof(struct cvirt_io_inode));
		} else if (entry->inode->stat.st_nlink > 1) {
			entry->inode->stat.st_nlink--;
			entry->inode = cvirt_xcalloc(1, sizeof(struct cvirt_io_inode));
		}

		struct cvirt_io_inode *inode = entry->inode;
		copy_stat(&inode->stat, archive_entry_stat(archive_entry));

		set_xattr_from_libarchive(inode, archive_entry);

		if (S_ISLNK(inode->stat.st_mode)) {
			char *link = strdup(archive_entry_symlink(archive_entry));
			assert(link);
			inode->target = link;
		} else if ((flags & CVIRT_IO_TREE_CHECKSUM) &&
				S_ISREG(inode->stat.st_mode)) {
			checksum_from_archive(inode->sha256sum, archive,
				archive_entry_size(archive_entry), checksum_ctx);
		}
	}
	free(checksum_ctx);
	if (res == ARCHIVE_FATAL) {
		return -1;
	}

	return 0;
}

static void entry_cleanup(struct cvirt_io_entry *entry);

static int apply_layer_substration(struct cvirt_io_entry *root,
		struct cvirt_oci_r_layer *layer, uint32_t flags) {
	struct archive *archive = cvirt_oci_r_layer_get_libarchive(layer);
	struct archive_entry *archive_entry;
	int res;
	while ((res = archive_read_next_header(archive, &archive_entry)) != ARCHIVE_EOF
			&& res != ARCHIVE_FATAL) {
		const char *orig_name = archive_entry_pathname(archive_entry);
		char *basename_dup = cvirt_xstrdup(orig_name);
		const char *base = basename(basename_dup);
		if (strncmp(base, ".wh.", 4)) {
			// not whiteouts
			free(basename_dup);
			continue;
		}
		char *dir_dup = normalize_tar_entry_name(orig_name);
		const char *dir = dirname(dir_dup);
		struct cvirt_io_entry *dir_entry = find_entry(root, dir, false);
		if (!dir_entry || !S_ISDIR(dir_entry->inode->stat.st_mode)) {
			free(basename_dup);
			free(dir_dup);
			continue;
		}
		if (!strcmp(base, ".wh..wh..opq")) {
			for (int i = 0; i < dir_entry->inode->children_len; i++) {
				entry_cleanup(&dir_entry->inode->children[i]);
			}
			dir_entry->inode->children_len = 0;
		} else {
			const char *realname = &base[4];
			if (!*realname) {
				free(basename_dup);
				free(dir_dup);
				continue;
			}

			for (int i = 0; i < dir_entry->inode->children_len; i++) {
				if (!strcmp(realname, dir_entry->inode->children[i].name)) {
					entry_cleanup(&dir_entry->inode->children[i]);
					memmove(&dir_entry->inode->children[i],
						&dir_entry->inode->children[i + 1],
						(dir_entry->inode->children_len - 1 - i) *
						sizeof(struct cvirt_io_entry));
					break;
				}
			}
		}
		free(basename_dup);
		free(dir_dup);
	}
	if (res == ARCHIVE_FATAL) {
		return -1;
	}
	return 0;
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
	result->inode = cvirt_xcalloc(1, sizeof(struct cvirt_io_inode));

	result->inode->stat.st_mode = S_IFDIR | 0755;

	int res = apply_layer_addition(result, layer, flags);
	if (res < 0) {
		cvirt_io_tree_destroy(result);
		return NULL;
	}

	return result;
}

int cvirt_io_tree_oci_apply_layer(struct cvirt_io_entry *root,
		struct cvirt_oci_r_layer *layer, uint32_t flags) {
	int res = apply_layer_substration(root, layer, flags);
	if (res < 0) {
		return res;
	}
	cvirt_oci_r_layer_rewind(layer);
	return apply_layer_addition(root, layer, flags);

}

static void inode_unref(struct cvirt_io_inode *inode) {
	if (!--inode->stat.st_nlink) {
		for (int i = 0; i < inode->xattrs_len; i++) {
			free(inode->xattrs[i].name);
			free(inode->xattrs[i].value);
		}
		free(inode->xattrs);

		if (S_ISDIR(inode->stat.st_mode)) {
			for (int i = 0; i < inode->children_len; i++) {
				entry_cleanup(&inode->children[i]);
			}
			free(inode->children);
		} else if (S_ISLNK(inode->stat.st_mode)) {
			free(inode->target);
		}
		free(inode);
	}
}

static void entry_cleanup(struct cvirt_io_entry *entry) {
	free(entry->name);
	inode_unref(entry->inode);
}

void cvirt_io_tree_destroy(struct cvirt_io_entry *entry) {
	if (!entry) {
		return;
	}

	entry_cleanup(entry);

	free(entry);
}
