#ifndef OCI_CONFIG_H
#define OCI_CONFIG_H

struct cvirt_oci_config {
	struct json_object *root_obj;
	struct json_object *diff_ids;
	struct json_object *config;
	struct json_object *config_env;
	const char *content;
};

#endif
