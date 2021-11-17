#include "convirter/oci/config.h"
#include "oci/config.h"
#include "oci/layer.h"
#include "goarch.h"

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

	struct json_object *arch = json_object_new_string(NATIVE_GOARCH);
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

static int config_ensure_config_object(struct cvirt_oci_config *config) {
	if (config->config) {
		return 0;
	}
	config->config = json_object_new_object();
	if (!config->config) {
		return -errno;
	}
	json_object_object_add(config->root_obj, "config", config->config);
	return 0;
}

int cvirt_oci_config_set_user(struct cvirt_oci_config *config, const char *user) {
	int res = config_ensure_config_object(config);
	if (res < 0) {
		return res;
	}
	struct json_object *user_obj = json_object_new_string(user);
	if (!user_obj) {
		return -errno;
	}
	json_object_object_add(config->config, "User", user_obj);
	return 0;
}

int cvirt_oci_config_set_cmd(struct cvirt_oci_config *config, char *const cmd[]) {
	int res = config_ensure_config_object(config);
	if (res < 0) {
		return res;
	}
	struct json_object *cmd_arr = json_object_new_array();
	if (!cmd_arr) {
		return -errno;
	}
	for (int i = 0; cmd[i]; i++) {
		struct json_object *str = json_object_new_string(cmd[i]);
		if (!str) {
			goto err;
		}
		json_object_array_add(cmd_arr, str);
	}
	json_object_object_add(config->config, "Cmd", cmd_arr);
	return 0;
err:
	res = errno;
	json_object_put(cmd_arr);
	return -res;
}

int cvirt_oci_config_set_entrypoint(struct cvirt_oci_config *config, char *const entrypoint[]) {
	int res = config_ensure_config_object(config);
	if (res < 0) {
		return res;
	}
	struct json_object *entrypoint_arr = json_object_new_array();
	if (!entrypoint_arr) {
		return -errno;
	}
	for (int i = 0; entrypoint[i]; i++) {
		struct json_object *str = json_object_new_string(entrypoint[i]);
		if (!str) {
			goto err;
		}
		json_object_array_add(entrypoint_arr, str);
	}
	json_object_object_add(config->config, "Entrypoint", entrypoint_arr);
	return 0;
err:
	res = errno;
	json_object_put(entrypoint_arr);
	return -res;
}

int cvirt_oci_config_set_stop_signal(struct cvirt_oci_config *config, const char *signal) {
	int res = config_ensure_config_object(config);
	if (res < 0) {
		return res;
	}
	struct json_object *signal_obj = json_object_new_string(signal);
	if (!signal_obj) {
		return -errno;
	}
	json_object_object_add(config->config, "StopSignal", signal_obj);
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
