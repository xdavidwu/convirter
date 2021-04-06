#ifndef OCI_CONFIG_H
#define OCI_CONFIG_H

struct cvirt_oci_config {
	struct json_object *root_obj;
	struct json_object *diff_ids;
	const char *content;
};

#endif
