#ifndef CVIRT_OCI_R_MANIFEST_H
#define CVIRT_OCI_R_MANIFEST_H

struct cvirt_oci_r_manifest;

enum cvirt_oci_r_layer_compression;

struct cvirt_oci_r_manifest *cvirt_oci_r_manifest_from_archive_blob(
	const char *path, const char *digest);

const char *cvirt_oci_r_manifest_get_config_digest(
	struct cvirt_oci_r_manifest *manifest);

int cvirt_oci_r_manifest_get_layers_length(
	struct cvirt_oci_r_manifest *manifest);

const char *cvirt_oci_r_manifest_get_layer_digest(
	struct cvirt_oci_r_manifest *manifest, int index);

enum cvirt_oci_r_layer_compression cvirt_oci_r_manifest_get_layer_compression(
	struct cvirt_oci_r_manifest *manifest, int index);

void cvirt_oci_r_manifest_destroy(struct cvirt_oci_r_manifest *manifest);

#endif
