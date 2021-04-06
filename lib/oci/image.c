#include "convirter/oci/image.h"
#include "oci/blob.h"
#include "oci/image.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char oci_layout_str[] = "{\"imageLayoutVersion\":\"1.0.0\"}";

static const size_t bufsz = 1024 * 1024;

struct cvirt_oci_image *cvirt_oci_image_new(const char *path) {
	int res;
	struct cvirt_oci_image *image = calloc(1, sizeof(struct cvirt_oci_image));
	if (!image) {
		goto err;
	}

	image->archive = archive_write_new();
	if (!image->archive) {
		goto err;
	}
	res = archive_write_set_format_pax_restricted(image->archive);
	if (res < 0) {
		goto err;
	}
	res = archive_write_open_filename(image->archive, path);
	if (res < 0) {
		goto err;
	}

	image->index_obj = json_object_new_object();
	if (!image->index_obj) {
		goto err;
	}

	struct json_object *version = json_object_new_int(2);
	if (!version) {
		goto err;
	}
	json_object_object_add(image->index_obj, "schemaVersion", version);

	image->manifests = json_object_new_array();
	if (!image->manifests) {
		goto err;
	}
	json_object_object_add(image->index_obj, "manifests", image->manifests);

	image->entry = archive_entry_new();
	if (!image->entry) {
		goto err;
	}
	size_t sz = strlen(oci_layout_str);
	archive_entry_set_pathname(image->entry, "oci-layout");
	archive_entry_set_filetype(image->entry, AE_IFREG);
	archive_entry_set_size(image->entry, sz);
	archive_write_header(image->archive, image->entry);
	archive_write_data(image->archive, oci_layout_str, sz);

	archive_entry_clear(image->entry);
	archive_entry_set_pathname(image->entry, "blobs");
	archive_entry_set_filetype(image->entry, AE_IFDIR);
	archive_write_header(image->archive, image->entry);

	archive_entry_clear(image->entry);
	archive_entry_set_pathname(image->entry, "blobs/sha256");
	archive_entry_set_filetype(image->entry, AE_IFDIR);
	archive_write_header(image->archive, image->entry);

	return image;
		
err:
	if (image) {
		json_object_put(image->index_obj);
		archive_write_free(image->archive);
	}
	free(image);
	return NULL;
}

int cvirt_oci_image_add_blob(struct cvirt_oci_image *image, struct cvirt_oci_blob *blob) {
	char path[6 + 7 + 64 + 1];
	strcpy(path, "blobs/sha256/");
	strcat(path, blob->sha256);

	archive_entry_clear(image->entry);
	archive_entry_set_pathname(image->entry, path);
	archive_entry_set_filetype(image->entry, AE_IFREG);
	archive_entry_set_size(image->entry, blob->size);
	archive_write_header(image->archive, image->entry);

	if (blob->from_mem) {
		archive_write_data(image->archive, blob->content, blob->size);
	} else {
		char buf[bufsz];
		size_t sz = blob->size;
		int fd = open(blob->path, O_RDONLY);
		if (!fd) {
			perror("open");
			return -errno;
		}
		while (sz) {
			ssize_t r = read(fd, buf, sz > bufsz ? bufsz : sz);
			if (r < 0) {
				perror("read");
				return -errno;
			} else if (r == 0) {
				fprintf(stderr, "%s ends permaturely: %ld remaining\n", blob->path, sz);
				return -1;
			}
			sz -= r;
			if (archive_write_data(image->archive, buf, r) < 0) {
				return -errno;
			}
		}
		close(fd);
	}
	return 0;
}

int cvirt_oci_image_add_manifest(struct cvirt_oci_image *image, struct cvirt_oci_blob *blob) {
	struct json_object *descriptor = descriptor_from_oci_blob(blob);
	if (!descriptor) {
		return -1;
	}
	json_object_array_add(image->manifests, descriptor);
	return cvirt_oci_image_add_blob(image, blob);
}

int cvirt_oci_image_close(struct cvirt_oci_image *image) {
	const char *index = json_object_to_json_string(image->index_obj);
	size_t sz = strlen(index);
	archive_entry_clear(image->entry);
	archive_entry_set_pathname(image->entry, "index.json");
	archive_entry_set_filetype(image->entry, AE_IFREG);
	archive_entry_set_size(image->entry, sz);
	archive_write_header(image->archive, image->entry);
	archive_write_data(image->archive, index, sz);

	archive_entry_free(image->entry);
	json_object_put(image->index_obj);
	archive_write_close(image->archive);
	archive_write_free(image->archive);
	return 0;
}

void cvirt_oci_image_destroy(struct cvirt_oci_image *image) {
	free(image);
}
