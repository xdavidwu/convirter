#include "convirter/oci/layer.h"
#include "oci/layer.h"
#include "compressor.h"
#include "sha256.h"

#include <stdlib.h>
#include <string.h>

#include <archive.h>

#define LAYER_FILE_TEMPLATE	"/cvirt-oci-layer-XXXXXX"

static const char media_type_zstd[] = "application/vnd.oci.image.layer.v1.tar+zstd";

struct cvirt_oci_layer *cvirt_oci_layer_new() {
	int res;
	struct cvirt_oci_layer *layer = calloc(1, sizeof(struct cvirt_oci_layer));
	if (!layer) {
		goto out;
	}

	layer->media_type = media_type_zstd;

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

int cvirt_oci_layer_close(struct cvirt_oci_layer *layer) {
	archive_write_close(layer->archive);
	archive_write_free(layer->archive);
	layer->diff_id = sha256sum_from_file(layer->tmp_filename);
	if (!layer->diff_id) {
		return -1;
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
	int res = compress(layer->fd, compressed_fd);
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

int cvirt_oci_layer_free(struct cvirt_oci_layer *layer) {
	free(layer->tmp_filename);
	free(layer->diff_id);
	free(layer->compressed_filename);
	return 0;
}
