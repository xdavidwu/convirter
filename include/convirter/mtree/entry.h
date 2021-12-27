#ifndef CVIRT_MTREE_ENTRY_H
#define CVIRT_MTREE_ENTRY_H

#include <sys/stat.h>
#include <stdint.h>

#include <guestfs.h>

#include <convirter/oci-r/layer.h>

struct cvirt_mtree_inode {
	struct stat stat;

	struct cvirt_mtree_xattr *xattrs;
	unsigned int xattrs_len;
	unsigned int xattrs_capacity;

	union { // data
		uint8_t sha256sum[32]; // S_IFREG
		struct { // S_IFDIR
			struct cvirt_mtree_entry *children;
			unsigned int children_len;
			unsigned int children_capacity;
		};
		char *target; // S_IFLNK
	};
};

struct cvirt_mtree_entry {
	char *name;

	struct cvirt_mtree_inode *inode;

	void *userdata;
};

enum cvirt_mtree_tree_flags {
	CVIRT_MTREE_TREE_CHECKSUM = 1 << 0,
	CVIRT_MTREE_TREE_GUESTFS_BTRFS_SKIP_SNAPSHOTS = 1 << 1,
};

struct cvirt_mtree_entry *cvirt_mtree_tree_from_guestfs(guestfs_h *guestfs, uint32_t flags);

struct cvirt_mtree_entry *cvirt_mtree_tree_from_oci_layer(struct cvirt_oci_r_layer *layer, uint32_t flags);

int cvirt_mtree_tree_oci_apply_layer(struct cvirt_mtree_entry *root, struct cvirt_oci_r_layer *layer, uint32_t flags);

void cvirt_mtree_tree_destroy(struct cvirt_mtree_entry *entry);

#endif
