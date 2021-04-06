#include "convirter/oci/blob.h"
#include "oci/blob.h"
#include "oci/config.h"
#include "oci/layer.h"
#include "oci/manifest.h"
#include "sha256.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct cvirt_oci_blob *cvirt_oci_blob_from_layer(struct cvirt_oci_layer *layer) {
	struct cvirt_oci_blob *blob = NULL;
       	blob = calloc(1, sizeof(struct cvirt_oci_blob));
	if (!blob) {
		return NULL;
	}

	blob->media_type = layer->media_type;

	blob->path = strdup(layer->compressed_filename);
	if (!blob->path) {
		free(blob);
		return NULL;
	}

	blob->sha256 = sha256sum_from_file(blob->path);
	if (!blob->sha256) {
		free(blob->path);
		free(blob);
		return NULL;
	}

	struct stat st;
	if (stat(blob->path, &st) < 0) {
		perror("stat");
		free(blob->path);
		free(blob);
		return NULL;
	}
	blob->size = st.st_size;

	return blob;
}

struct cvirt_oci_blob *cvirt_oci_blob_from_config(struct cvirt_oci_config *config) {
	struct cvirt_oci_blob *blob = NULL;
       	blob = calloc(1, sizeof(struct cvirt_oci_blob));
	if (!blob) {
		return NULL;
	}

	blob->from_mem = true;
	blob->media_type = "application/vnd.oci.image.config.v1+json";
	blob->content = config->content;
	blob->size = strlen(blob->content);

	blob->sha256 = sha256sum_from_mem(blob->content, blob->size);
	if (!blob->sha256) {
		free(blob->path);
		free(blob);
		return NULL;
	}

	return blob;
}

struct cvirt_oci_blob *cvirt_oci_blob_from_manifest(struct cvirt_oci_manifest *manifest) {
	struct cvirt_oci_blob *blob = NULL;
       	blob = calloc(1, sizeof(struct cvirt_oci_blob));
	if (!blob) {
		return NULL;
	}

	blob->from_mem = true;
	blob->media_type = "application/vnd.oci.image.manifest.v1+json";
	blob->content = manifest->content;
	blob->size = strlen(blob->content);

	blob->sha256 = sha256sum_from_mem(blob->content, blob->size);
	if (!blob->sha256) {
		free(blob->path);
		free(blob);
		return NULL;
	}

	return blob;
}

struct json_object *descriptor_from_oci_blob(struct cvirt_oci_blob *blob) {
	struct json_object *root_obj = json_object_new_object();
	if (!root_obj) {
		goto err;
	}

	struct json_object *type = json_object_new_string(blob->media_type);
	if (!type) {
		goto err;
	}
	json_object_object_add(root_obj, "mediaType", type);

	char digest_str[7 + 64 + 1];
	sprintf(digest_str, "sha256:%s", blob->sha256);
	struct json_object *digest = json_object_new_string(digest_str);
	if (!digest) {
		goto err;
	}
	json_object_object_add(root_obj, "digest", digest);

	struct json_object *size = json_object_new_int64(blob->size);
	if (!size) {
		goto err;
	}
	json_object_object_add(root_obj, "size", size);

	return root_obj;
err:
	json_object_put(root_obj);
	return NULL;
}

void cvirt_oci_blob_destory(struct cvirt_oci_blob *blob) {
	free(blob->sha256);
	free(blob->path);
	free(blob);
}

