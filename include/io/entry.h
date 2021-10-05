#ifndef IO_ENTRY_H
#define IO_ENTRY_H

#include "list.h"

#include <stdint.h>

#include <gcrypt.h>

#define IO_ENTRY_GUESTFS_BUF_LEN	(4000 * 1024)

struct io_entry_oci_checksum_ctx {
	gcry_md_hd_t gcrypt_handle;
};

struct io_entry_guestfs_ctx {
	struct cvirt_list *hardlink_inodes;
	gcry_md_hd_t gcrypt_handle;
};

#endif
