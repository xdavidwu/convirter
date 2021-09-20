#include "archive-utils.h"
#include "convirter/oci-r/index.h"
#include "oci-r/index.h"
#include "goarch.h"

#include <json_object.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct cvirt_oci_r_index *cvirt_oci_r_index_from_archive(int fd) {
	struct cvirt_oci_r_index *index = calloc(1,
		sizeof(struct cvirt_oci_r_index));
	if (!index) {
		goto err;
	}
	assert(lseek(fd, 0, SEEK_SET) == 0);
	index->obj = json_from_archive(fd, "index.json");
	if (!index->obj) {
		goto err;
	}
	struct json_object *schema_obj = json_object_object_get(index->obj,
		"schemaVersion");
	if (!schema_obj) {
		goto err;
	}
	int schema_version = json_object_get_int(schema_obj);
	if (schema_version != 2) {
		goto err;
	}
	return index;
err:
	free(index);
	return NULL;
}

const char *cvirt_oci_r_index_get_native_manifest_digest(
		struct cvirt_oci_r_index *index) {
	struct json_object *manifests_obj = json_object_object_get(index->obj,
		"manifests");
	if (!manifests_obj) {
		return NULL;
	}
	int len = json_object_array_length(manifests_obj);
	for (int i = 0; i < len; i++) {
		struct json_object *candidate = json_object_array_get_idx(
			manifests_obj, i);
		struct json_object *type = json_object_object_get(candidate,
			"mediaType");
		struct json_object *digest = json_object_object_get(candidate,
			"digest");
		if (!type || !digest) {
			goto err;
		}
		if (strcmp(json_object_get_string(type),
				"application/vnd.oci.image.manifest.v1+json")) {
			continue;
		}
		struct json_object *constraints = json_object_object_get(
			candidate, "platform");
		if (!constraints) {
			return json_object_get_string(digest);
		}
		struct json_object *architecture = json_object_object_get(
			constraints, "architecture");
		struct json_object *os = json_object_object_get(constraints, "os");
		if (!architecture || !os) {
			goto err;
		}
		if (strcmp(json_object_get_string(architecture), NATIVE_GOARCH) ||
				strcmp(json_object_get_string(os), "linux")) {
			continue;
		}
		return json_object_get_string(digest);
	}
err:
	return NULL;
}

void cvirt_oci_r_index_destroy(struct cvirt_oci_r_index *index) {
	json_object_put(index->obj);
	free(index);
}
