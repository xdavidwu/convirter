#ifndef CVIRT_OCI_R_LAYER_H
#define CVIRT_OCI_R_LAYER_H

struct cvirt_oci_r_layer;

enum cvirt_oci_r_layer_compression {
	CVIRT_OCI_R_LAYER_COMPRESSION_NONE,
	CVIRT_OCI_R_LAYER_COMPRESSION_ZSTD,
	CVIRT_OCI_R_LAYER_COMPRESSION_GZIP,
};

struct cvirt_oci_r_layer *cvirt_oci_r_layer_from_archive_blob(
	const char *path, const char *digest,
	enum cvirt_oci_r_layer_compression compression);

struct archive *cvirt_oci_r_layer_get_libarchive(struct cvirt_oci_r_layer *layer);

void cvirt_oci_r_layer_destroy(struct cvirt_oci_r_layer *layer);

#endif
