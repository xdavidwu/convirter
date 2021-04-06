#include "convirter/oci/config.h"
#include "oci/config.h"
#include "oci/layer.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#include <json-c/json_tokener.h>

struct cvirt_oci_config *cvirt_oci_config_new() {
	struct cvirt_oci_config *config = calloc(1, sizeof(struct cvirt_oci_config));
	if (!config) {
		goto err;
	}
	config->root_obj = json_object_new_object();
	if (!config->root_obj) {
		goto err;
	}

	// TODO
	struct json_object *arch = json_object_new_string("amd64");
	if (!arch) {
		goto err;
	}
	json_object_object_add(config->root_obj, "architecture", arch);

	struct json_object *os = json_object_new_string("linux");
	if (!os) {
		goto err;
	}
	json_object_object_add(config->root_obj, "os", os);

	struct json_object *rootfs = json_object_new_object();
	if (!rootfs) {
		goto err;
	}
	json_object_object_add(config->root_obj, "rootfs", rootfs);

	struct json_object *rootfs_type = json_object_new_string("layers");
	if (!rootfs_type) {
		goto err;
	}
	json_object_object_add(rootfs, "type", rootfs_type);

	config->diff_ids = json_object_new_array();
	if (!config->diff_ids) {
		goto err;
	}
	json_object_object_add(rootfs, "diff_ids", config->diff_ids);

	return config;

err:
	if (config) {
		json_object_put(config->root_obj);
	}
	free(config);
	return NULL;
}

int cvirt_oci_config_add_layer(struct cvirt_oci_config *config, struct cvirt_oci_layer *layer) {
	assert(layer->diff_id);
	struct json_object *diff_id = json_object_new_string(layer->diff_id);
	if (!diff_id) {
		return -errno;
	}
	json_object_array_add(config->diff_ids, diff_id);
	return 0;
}

int cvirt_oci_config_close(struct cvirt_oci_config *config) {
	config->content = json_object_to_json_string_ext(config->root_obj, JSON_C_TO_STRING_PLAIN);
	return config->content ? 0 : -errno;
}

void cvirt_oci_config_destroy(struct cvirt_oci_config *config) {
	json_object_put(config->root_obj);
	free(config);
}
