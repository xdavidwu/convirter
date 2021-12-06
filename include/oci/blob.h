#ifndef OCI_BLOB_H
#define OCI_BLOB_H

#include <stdbool.h>
#include <json-c/json_tokener.h>

struct cvirt_oci_blob {
	const char *media_type;

	enum {
		STORE_FS,
		STORE_MEM,
		STORE_ARCHIVE,
	} store_type;

	union {
		char *path;
		const char *content;
		struct archive *from_archive;
	};

	size_t size;

	enum {
		DIGEST_SHA256,
		DIGEST_PREFIXED
	} digest_type;
	union {
		char *sha256;
		char *digest;
	};
};

struct json_object *descriptor_from_oci_blob(struct cvirt_oci_blob *blob);

#endif
