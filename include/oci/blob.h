#ifndef OCI_BLOB_H
#define OCI_BLOB_H

#include <stdbool.h>
#include <json-c/json_tokener.h>

struct cvirt_oci_blob {
	bool from_mem;
	const char *media_type;

	char *path;

	const char *content;
	size_t size;

	char *sha256;
};

struct json_object *descriptor_from_oci_blob(struct cvirt_oci_blob *blob);

#endif
