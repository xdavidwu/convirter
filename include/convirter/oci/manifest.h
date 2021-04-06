#ifndef CVIRT_OCI_MANIFEST_H
#define CVIRT_OCI_MANIFEST_H

struct cvirt_oci_manifest;
struct cvirt_oci_blob;

struct cvirt_oci_manifest *cvirt_oci_manifest_new();

int cvirt_oci_manifest_set_config(struct cvirt_oci_manifest *manifest,
	struct cvirt_oci_blob *blob);

int cvirt_oci_manifest_add_layer(struct cvirt_oci_manifest *manifest,
	struct cvirt_oci_blob *blob);

int cvirt_oci_manifest_close(struct cvirt_oci_manifest *manifest);

void cvirt_oci_manifest_destroy(struct cvirt_oci_manifest *manifest);

#endif
