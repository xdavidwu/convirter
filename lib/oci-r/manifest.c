#include "archive-utils.h"
#include "convirter/oci-r/layer.h"
#include "convirter/oci-r/manifest.h"
#include "oci-r/manifest.h"

#include <errno.h>
#include <json_object.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cvirt_oci_r_manifest *cvirt_oci_r_manifest_from_archive_blob(
		const char *path, const char *digest) {
	struct cvirt_oci_r_manifest *manifest = calloc(1,
		sizeof(struct cvirt_oci_r_manifest));
	if (!manifest) {
		goto err;
	}
	char *name = digest_to_name(digest);
	if (!name) {
		goto err;
	}
	manifest->obj = json_from_archive(path, name);
	free(name);
	if (!manifest->obj) {
		goto err;
	}
	struct json_object *schema_obj = json_object_object_get(manifest->obj,
		"schemaVersion");
	if (!schema_obj) {
		goto err;
	}
	int schema_version = json_object_get_int(schema_obj);
	if (schema_version != 2) {
		goto err;
	}
	return manifest;
err:
	free(manifest);
	return NULL;
}

const char *cvirt_oci_r_manifest_get_config_digest(
		struct cvirt_oci_r_manifest *manifest) {
	struct json_object *config = json_object_object_get(manifest->obj, "config");
	if (!config) {
		return NULL;
	}
	struct json_object *media_type = json_object_object_get(config, "mediaType");
	if (!media_type || strcmp("application/vnd.oci.image.config.v1+json",
			json_object_get_string(media_type))) {
		return NULL;
	}
	struct json_object *digest = json_object_object_get(config, "digest");
	if (!digest) {
		return NULL;
	}
	return json_object_get_string(digest);
}

int cvirt_oci_r_manifest_get_layers_length(struct cvirt_oci_r_manifest *manifest) {
	struct json_object *layers = json_object_object_get(manifest->obj, "layers");
	if (!layers) {
		return -EINVAL;
	}
	return json_object_array_length(layers);
}

const char *cvirt_oci_r_manifest_get_layer_digest(
		struct cvirt_oci_r_manifest *manifest, int index) {
	struct json_object *layers = json_object_object_get(manifest->obj, "layers");
	if (!layers) {
		return NULL;
	}
	struct json_object *layer = json_object_array_get_idx(layers, index);
	if (!layer) {
		return NULL;
	}
	struct json_object *digest = json_object_object_get(layer, "digest");
	if (!digest) {
		return NULL;
	}
	return json_object_get_string(digest);
}

enum cvirt_oci_r_layer_compression cvirt_oci_r_manifest_get_layer_compression(
		struct cvirt_oci_r_manifest *manifest, int index) {
	struct json_object *layers = json_object_object_get(manifest->obj, "layers");
	if (!layers) {
		return -EINVAL;
	}
	struct json_object *layer = json_object_array_get_idx(layers, index);
	if (!layer) {
		return -EINVAL;
	}
	struct json_object *media_type = json_object_object_get(layer, "mediaType");
	if (!media_type) {
		return -EINVAL;
	}
	const char *type = json_object_get_string(media_type);
	fprintf(stderr, "cvirt_oci_r_manifest_get_layer_compression: mediaType: %s\n", type);
	if (!strcmp("application/vnd.oci.image.layer.v1.tar", type)) {
		return CVIRT_OCI_R_LAYER_COMPRESSION_NONE;
	} else if (!strcmp("application/vnd.oci.image.layer.v1.tar+zstd", type)) {
		return CVIRT_OCI_R_LAYER_COMPRESSION_ZSTD;
	} else if (!strcmp("application/vnd.oci.image.layer.v1.tar+gzip", type)) {
		return CVIRT_OCI_R_LAYER_COMPRESSION_GZIP;
	} else if (!strcmp("application/vnd.oci.image.layer.nondistributable.v1.tar", type)) {
		return CVIRT_OCI_R_LAYER_COMPRESSION_NONE;
	} else if (!strcmp("application/vnd.oci.image.layer.nondistributable.v1.tar+zstd", type)) {
		return CVIRT_OCI_R_LAYER_COMPRESSION_ZSTD;
	} else if (!strcmp("application/vnd.oci.image.layer.nondistributable.v1.tar+gzip", type)) {
		return CVIRT_OCI_R_LAYER_COMPRESSION_GZIP;
	}
	return -EINVAL;
}

void cvirt_oci_r_manifest_destroy(struct cvirt_oci_r_manifest *manifest) {
	json_object_put(manifest->obj);
	free(manifest);
}
