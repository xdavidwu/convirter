#include <convirter/io/entry.h>
#include <convirter/io/xattr.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "hex.h"

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
