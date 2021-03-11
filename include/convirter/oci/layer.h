#ifndef CVIRT_OCI_LAYER_H
#define CVIRT_OCI_LAYER_H

struct cvirt_oci_layer;

struct cvirt_oci_layer *cvirt_oci_layer_new();

int cvirt_oci_layer_close(struct cvirt_oci_layer *layer);

const char *cvirt_oci_layer_get_path(struct cvirt_oci_layer *layer);

struct archive *cvirt_oci_layer_get_libarchive(struct cvirt_oci_layer *layer);

int cvirt_oci_layer_free(struct cvirt_oci_layer *layer);

#endif
