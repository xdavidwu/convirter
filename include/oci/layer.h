#ifndef OCI_LAYER_H
#define OCI_LAYER_H

#include "convirter/oci/layer.h"

struct cvirt_oci_layer {
	const char *media_type;
	enum cvirt_oci_layer_compression compression;
	int compression_level;
	char *tmp_filename;
	char *compressed_filename;
	char *diff_id;
	int fd;
	struct archive *archive;
};

#endif
