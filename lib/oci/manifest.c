#include "convirter/oci/manifest.h"
#include "oci/blob.h"
#include "oci/manifest.h"

#include <errno.h>
#include <stdlib.h>

#include <json-c/json_tokener.h>

struct cvirt_oci_manifest *cvirt_oci_manifest_new() {
	struct cvirt_oci_manifest *manifest = calloc(1, sizeof(struct cvirt_oci_manifest));
	if (!manifest) {
		goto err;
	}
	manifest->root_obj = json_object_new_object();
	if (!manifest->root_obj) {
		goto err;
	}

	struct json_object *version = json_object_new_int(2);
	if (!version) {
		goto err;
	}
	json_object_object_add(manifest->root_obj, "schemaVersion", version);

	manifest->layers = json_object_new_array();
	if (!manifest->layers) {
		goto err;
	}
	json_object_object_add(manifest->root_obj, "layers", manifest->layers);

	return manifest;

err:
	if (manifest) {
		json_object_put(manifest->root_obj);
	}
	free(manifest);
	return NULL;
}

int cvirt_oci_manifest_set_config(struct cvirt_oci_manifest *manifest,
		struct cvirt_oci_blob *blob) {
	struct json_object *obj = descriptor_from_oci_blob(blob);
	if (!obj) {
		return -1;
	}
	json_object_object_add(manifest->root_obj, "config", obj);
	return 0;
}

int cvirt_oci_manifest_add_layer(struct cvirt_oci_manifest *manifest,
		struct cvirt_oci_blob *blob) {
	struct json_object *obj = descriptor_from_oci_blob(blob);
	if (!obj) {
		return -1;
	}
	json_object_array_add(manifest->layers, obj);
	return 0;
}

int cvirt_oci_manifest_close(struct cvirt_oci_manifest *manifest) {
	manifest->content = json_object_to_json_string_ext(manifest->root_obj, JSON_C_TO_STRING_PLAIN);
	return manifest->content ? 0 : -errno;
}

void cvirt_oci_manifest_destroy(struct cvirt_oci_manifest *manifest) {
	json_object_put(manifest->root_obj);
	free(manifest);
}
