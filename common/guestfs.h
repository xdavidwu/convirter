#ifndef GUESTFS_H
#define GUESTFS_H

#include <guestfs.h>

guestfs_h *create_guestfs_mount_first_linux(const char *image,
	char ***succeeded_mounts);
guestfs_h *create_qcow2_btrfs_image(const char *path, size_t size);

#endif
