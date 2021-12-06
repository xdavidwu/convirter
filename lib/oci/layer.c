#include "convirter/oci/layer.h"
#include "convirter/oci-r/layer.h"
#include "oci/layer.h"
#include "oci-r/layer.h"
#include "archive-utils.h"
#include "compressor.h"
#include "sha256.h"
#include "xmem.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <archive.h>

#define LAYER_FILE_TEMPLATE	"/cvirt-oci-layer-XXXXXX"

static const char media_type_zstd[] = "application/vnd.oci.image.layer.v1.tar+zstd";
static const char media_type_gzip[] = "application/vnd.oci.image.layer.v1.tar+gzip";
static const char media_type_raw[] = "application/vnd.oci.image.layer.v1.tar";

struct cvirt_oci_layer *cvirt_oci_layer_new(
		enum cvirt_oci_layer_compression compression, int level) {
	int res;
	struct cvirt_oci_layer *layer = calloc(1, sizeof(struct cvirt_oci_layer));
	if (!layer) {
		goto out;
	}
	layer->layer_type = NEW_LAYER;

	layer->compression = compression;
	layer->compression_level = level;

	switch (layer->compression) {
	case CVIRT_OCI_LAYER_COMPRESSION_ZSTD:
		layer->media_type = media_type_zstd;
		break;
	case CVIRT_OCI_LAYER_COMPRESSION_GZIP:
		layer->media_type = media_type_gzip;
		break;
	case CVIRT_OCI_LAYER_COMPRESSION_NONE:
		layer->media_type = media_type_raw;
		break;
	}

	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir) {
		layer->tmp_filename = calloc(strlen(tmpdir) + strlen(LAYER_FILE_TEMPLATE) + 1, sizeof(char));
		if (!layer->tmp_filename) {
			free(layer);
			layer = NULL;
			goto out;
		}
		strcpy(layer->tmp_filename, tmpdir);
		strcat(layer->tmp_filename, LAYER_FILE_TEMPLATE);
	} else {
		layer->tmp_filename = strdup("/tmp" LAYER_FILE_TEMPLATE);
		if (!layer->tmp_filename) {
			free(layer);
			layer = NULL;
			goto out;
		}
	}
	layer->fd = mkstemp(layer->tmp_filename);
	if (layer->fd < 0) {
		free(layer);
		layer = NULL;
		goto out;
	}

	layer->archive = archive_write_new();
	if (!layer->archive) {
		free(layer);
		layer = NULL;
		goto out;
	}
	res = archive_write_set_format_pax_restricted(layer->archive);
	if (res < 0) {
		archive_write_free(layer->archive);
		free(layer);
		layer = NULL;
		goto out;
	}
	res = archive_write_set_format_option(layer->archive, "pax", "xattrheader", "LIBARCHIVE");
	if (res < 0) {
		archive_write_free(layer->archive);
		free(layer);
		layer = NULL;
		goto out;
	}
	res = archive_write_open_fd(layer->archive, layer->fd);
	if (res < 0) {
		archive_write_free(layer->archive);
		free(layer);
		layer = NULL;
		goto out;
	}

out:
	return layer;
}

struct cvirt_oci_layer *cvirt_oci_layer_from_archive_blob(int fd,
		const char *digest,
		enum cvirt_oci_r_layer_compression *compression,
		const char *diff_id) {
	struct cvirt_oci_layer *layer = calloc(1, sizeof(struct cvirt_oci_layer));
	if (!layer) {
		return NULL;
	}
	layer->layer_type = EXISTING_BLOB_FROM_ARCHIVE;
	switch (layer->compression) {
	case CVIRT_OCI_R_LAYER_COMPRESSION_ZSTD:
		layer->media_type = media_type_zstd;
		break;
	case CVIRT_OCI_R_LAYER_COMPRESSION_GZIP:
		layer->media_type = media_type_gzip;
		break;
	case CVIRT_OCI_R_LAYER_COMPRESSION_NONE:
		layer->media_type = media_type_raw;
		break;
	}
	char *name = digest_to_name(digest);
	struct archive_entry *entry;
	assert(lseek(fd, 0, SEEK_SET) == 0);
	layer->from_archive = archive_from_fd_and_seek(fd, name, &entry);
	free(name);
	layer->size = archive_entry_size(entry);
	if (!layer->from_archive) {
		free(layer);
		return NULL;
	}
	int index = 0;
	while (digest[index] != ':') {
		index++;
	}
	layer->digest = cvirt_xstrdup(digest);
	layer->diff_id = cvirt_xstrdup(diff_id);
	return layer;
}

int cvirt_oci_layer_close(struct cvirt_oci_layer *layer) {
	assert(layer->layer_type == NEW_LAYER);
	archive_write_close(layer->archive);
	archive_write_free(layer->archive);
	layer->diff_id_sha256 = sha256sum_from_file(layer->tmp_filename);
	if (!layer->diff_id_sha256) {
		return -1;
	}

	enum compression compression;
	switch (layer->compression) {
	case CVIRT_OCI_LAYER_COMPRESSION_ZSTD:
		compression = COMPRESSION_ZSTD;
		break;
	case CVIRT_OCI_LAYER_COMPRESSION_GZIP:
		compression = COMPRESSION_GZIP;
		break;
	case CVIRT_OCI_LAYER_COMPRESSION_NONE:
		layer->compressed_filename = strdup(layer->tmp_filename);
		return 0;
	}

	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir) {
		layer->compressed_filename = calloc(strlen(tmpdir) + strlen(LAYER_FILE_TEMPLATE) + 1, sizeof(char));
		if (!layer->compressed_filename) {
			return -1;
		}
		strcpy(layer->compressed_filename, tmpdir);
		strcat(layer->compressed_filename, LAYER_FILE_TEMPLATE);
	} else {
		layer->compressed_filename = strdup("/tmp" LAYER_FILE_TEMPLATE);
		if (!layer->compressed_filename) {
			return -1;
		}
	}
	int compressed_fd = mkstemp(layer->compressed_filename);
	if (compressed_fd < 0) {
		return -1;
	}
	lseek(layer->fd, 0, SEEK_SET);

	int res = compress(layer->fd, compressed_fd, compression, layer->compression_level);
	if (res < 0) {
		fprintf(stderr, "compress layer failed: %d\n", res);
		return -1;
	}

	close(compressed_fd);
	close(layer->fd);
	unlink(layer->tmp_filename);
	return 0;
}

const char *cvirt_oci_layer_get_path(struct cvirt_oci_layer *layer) {
	return layer->tmp_filename;
}

struct archive *cvirt_oci_layer_get_libarchive(struct cvirt_oci_layer *layer) {
	return layer->archive;
}

void cvirt_oci_layer_destroy(struct cvirt_oci_layer *layer) {
	if (layer->layer_type == NEW_LAYER) {
		unlink(layer->compressed_filename);
		free(layer->tmp_filename);
		free(layer->diff_id_sha256);
		free(layer->compressed_filename);
	} else if (layer->layer_type == EXISTING_BLOB_FROM_ARCHIVE) {
		free(layer->digest);
		free(layer->diff_id);
	}
}
