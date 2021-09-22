#ifndef GUESTFS_H
#define GUESTFS_H

#include <guestfs.h>

guestfs_h *create_guestfs_mount_first_linux(const char *image,
	char ***succeeded_mounts);

#endif
