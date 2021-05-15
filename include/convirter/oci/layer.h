#ifndef CVIRT_OCI_LAYER_H
#define CVIRT_OCI_LAYER_H

struct cvirt_oci_layer;

enum cvirt_oci_layer_compression {
	CVIRT_OCI_LAYER_COMPRESSION_NONE,
	CVIRT_OCI_LAYER_COMPRESSION_ZSTD,
	CVIRT_OCI_LAYER_COMPRESSION_GZIP,
};

struct cvirt_oci_layer *cvirt_oci_layer_new(
	enum cvirt_oci_layer_compression compression, int level);

int cvirt_oci_layer_close(struct cvirt_oci_layer *layer);

const char *cvirt_oci_layer_get_path(struct cvirt_oci_layer *layer);

struct archive *cvirt_oci_layer_get_libarchive(struct cvirt_oci_layer *layer);

void cvirt_oci_layer_destroy(struct cvirt_oci_layer *layer);

#endif
