#ifndef CVIRT_OCI_BLOB_H
#define CVIRT_OCI_BLOB_H

struct cvirt_oci_blob;
struct cvirt_oci_layer;
struct cvirt_oci_config;
struct cvirt_oci_manifest;

struct cvirt_oci_blob *cvirt_oci_blob_from_layer(struct cvirt_oci_layer *layer);

struct cvirt_oci_blob *cvirt_oci_blob_from_config(struct cvirt_oci_config *config);

struct cvirt_oci_blob *cvirt_oci_blob_from_manifest(struct cvirt_oci_manifest *manifest);

void cvirt_oci_blob_destory(struct cvirt_oci_blob *blob);


#endif
