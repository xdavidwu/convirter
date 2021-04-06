#ifndef OCI_MANIFEST_H
#define OCI_MANIFEST_H

struct cvirt_oci_manifest {
	struct json_object *root_obj;
	struct json_object *layers;
	const char *content;
};

#endif
