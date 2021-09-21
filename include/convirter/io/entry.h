#ifndef CVIRT_IO_ENTRY_H
#define CVIRT_IO_ENTRY_H

#include <sys/stat.h>
#include <stdint.h>

#include <guestfs.h>

#include <convirter/oci-r/layer.h>

struct cvirt_io_entry {
	char *name;
	struct stat stat;

	struct cvirt_io_xattr *xattrs;
	unsigned int xattrs_len;
	unsigned int xattrs_capacity;

	union { // data
		uint8_t sha256sum[32]; // S_IFREG
		struct { // S_IFDIR
			struct cvirt_io_entry *children;
			unsigned int children_len;
			unsigned int children_capacity;
		};
		char *target; // S_IFLNK
	};
};

enum cvirt_io_tree_flags {
	CVIRT_IO_TREE_CHECKSUM = 1 << 0,
};

struct cvirt_io_entry *cvirt_io_tree_from_guestfs(guestfs_h *guestfs, uint32_t flags);

struct cvirt_io_entry *cvirt_io_tree_from_oci_layer(struct cvirt_oci_r_layer *layer, uint32_t flags);

int cvirt_io_tree_oci_apply_layer(struct cvirt_io_entry *root, struct cvirt_oci_r_layer *layer, uint32_t flags);

void cvirt_io_tree_destroy(struct cvirt_io_entry *entry);

#endif
