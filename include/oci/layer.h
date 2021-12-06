#ifndef OCI_LAYER_H
#define OCI_LAYER_H

#include "convirter/oci/layer.h"

#include <stddef.h>

struct cvirt_oci_layer {
	const char *media_type;

	enum {
		NEW_LAYER,
		EXISTING_BLOB_FROM_ARCHIVE,
	} layer_type;
	union {
		struct {
			enum cvirt_oci_layer_compression compression;
			int compression_level;
			char *tmp_filename;
			char *compressed_filename;
			int fd;
			struct archive *archive;
			char *diff_id_sha256;
		}; // NEW_LAYER
		struct {
			char *digest;
			struct archive *from_archive;
			size_t size;
			char *diff_id;
		}; // EXISTING_BLOB_FROM_ARCHIVE
	};
};

#endif
