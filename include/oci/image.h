#ifndef OCI_IMAGE_H
#define OCI_IMAGE_H

#include <archive.h>
#include <archive_entry.h>
#include <json-c/json_tokener.h>

struct cvirt_oci_image {
	struct archive *archive;
	struct archive_entry *entry;
	struct json_object *index_obj;
	struct json_object *manifests;
};

#endif
