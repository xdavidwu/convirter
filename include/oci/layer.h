#ifndef OCI_LAYER_H
#define OCI_LAYER_H

struct cvirt_oci_layer {
	const char *media_type;
	char *tmp_filename;
	char *compressed_filename;
	char *diff_id;
	int fd;
	struct archive *archive;
};

#endif
