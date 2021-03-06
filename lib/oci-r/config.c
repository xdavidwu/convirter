#include "archive-utils.h"
#include "convirter/oci-r/config.h"
#include "oci-r/config.h"

#include <json_object.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cvirt_oci_r_config *cvirt_oci_r_config_from_archive_blob(int fd,
		const char *digest) {
	struct cvirt_oci_r_config *config = calloc(1,
		sizeof(struct cvirt_oci_r_config));
	if (!config) {
		goto err;
	}
	char *name = digest_to_name(digest);
	if (!name) {
		goto err;
	}
	assert(lseek(fd, 0, SEEK_SET) == 0);
	config->obj = json_from_archive(fd, name);
	free(name);
	if (!config->obj) {
		goto err;
	}
	// cache
	config->config = json_object_object_get(config->obj, "config");
	config->rootfs = json_object_object_get(config->obj, "rootfs");
	return config;
err:
	free(config);
	return NULL;
}

int cvirt_oci_r_config_get_cmd_length(struct cvirt_oci_r_config *config) {
	if (!config->config) {
		return 0;
	}
	struct json_object *cmd = json_object_object_get(config->config, "Cmd");
	if (!cmd) {
		return 0;
	}
	return json_object_array_length(cmd);
}

const char *cvirt_oci_r_config_get_cmd_part(
		struct cvirt_oci_r_config *config, int index) {
	if (!config->config) {
		return NULL;
	}
	struct json_object *cmd = json_object_object_get(config->config, "Cmd");
	if (!cmd) {
		return NULL;
	}
	struct json_object *part = json_object_array_get_idx(cmd, index);
	if (!part) {
		return NULL;
	}
	return json_object_get_string(part);
}

int cvirt_oci_r_config_get_entrypoint_length(
		struct cvirt_oci_r_config *config) {
	if (!config->config) {
		return 0;
	}
	struct json_object *entrypoint = json_object_object_get(config->config,
		"Entrypoint");
	if (!entrypoint) {
		return 0;
	}
	return json_object_array_length(entrypoint);
}

const char *cvirt_oci_r_config_get_entrypoint_part(
		struct cvirt_oci_r_config *config, int index) {
	if (!config->config) {
		return NULL;
	}
	struct json_object *entrypoint = json_object_object_get(config->config,
		"Entrypoint");
	if (!entrypoint) {
		return NULL;
	}
	struct json_object *part = json_object_array_get_idx(entrypoint, index);
	if (!part) {
		return NULL;
	}
	return json_object_get_string(part);
}

int cvirt_oci_r_config_get_env_length(struct cvirt_oci_r_config *config) {
	if (!config->config) {
		return 0;
	}
	struct json_object *env = json_object_object_get(config->config, "Env");
	if (!env) {
		return 0;
	}
	return json_object_array_length(env);
}

const char *cvirt_oci_r_config_get_env(
		struct cvirt_oci_r_config *config, int index) {
	if (!config->config) {
		return NULL;
	}
	struct json_object *envs = json_object_object_get(config->config, "Env");
	if (!envs) {
		return NULL;
	}
	struct json_object *env = json_object_array_get_idx(envs, index);
	if (!env) {
		return NULL;
	}
	return json_object_get_string(env);
}

const char *cvirt_oci_r_config_get_user(struct cvirt_oci_r_config *config) {
	if (!config->config) {
		return NULL;
	}
	struct json_object *user = json_object_object_get(config->config, "User");
	if (!user) {
		return NULL;
	}
	return json_object_get_string(user);
}

const char *cvirt_oci_r_config_get_working_dir(
		struct cvirt_oci_r_config *config) {
	if (!config->config) {
		return NULL;
	}
	struct json_object *working_dir = json_object_object_get(config->config,
		"WorkingDir");
	if (!working_dir) {
		return NULL;
	}
	return json_object_get_string(working_dir);
} 

int cvirt_oci_r_config_get_diff_ids_length(struct cvirt_oci_r_config *config) {
	if (!config->rootfs) {
		return 0;
	}
	struct json_object *diff_ids = json_object_object_get(config->rootfs, "diff_ids");
	if (!diff_ids) {
		return 0;
	}
	return json_object_array_length(diff_ids);
}

const char *cvirt_oci_r_config_get_diff_id(
	struct cvirt_oci_r_config *config, int index) {
	if (!config->rootfs) {
		return NULL;
	}
	struct json_object *diff_ids = json_object_object_get(config->rootfs, "diff_ids");
	if (!diff_ids) {
		return NULL;
	}
	struct json_object *diff_id = json_object_array_get_idx(diff_ids, index);
	if (!diff_id) {
		return NULL;
	}
	return json_object_get_string(diff_id);
}

void cvirt_oci_r_config_destroy(struct cvirt_oci_r_config *config) {
	json_object_put(config->obj);
	free(config);
}
