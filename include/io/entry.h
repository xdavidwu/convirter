#ifndef IO_ENTRY_H
#define IO_ENTRY_H

#include <stdint.h>

#include <gcrypt.h>

#define IO_ENTRY_OCI_BUF_LEN	(4 * 1024)

struct io_entry_oci_checksum_ctx {
	uint8_t buffer[IO_ENTRY_OCI_BUF_LEN];
	gcry_md_hd_t gcrypt_handle;
};

#endif
