#include "convirter/oci/layer.h"

#include <stdlib.h>
#include <string.h>

#include <archive.h>

#define LAYER_FILE_TEMPLATE	"/cvirt-oci-layer-XXXXXX"

struct cvirt_oci_layer {
	char *tmp_filename;
	int fd;
	struct archive *archive;
};

struct cvirt_oci_layer *cvirt_oci_layer_new() {
	char *template = NULL;
	int res;
	struct cvirt_oci_layer *layer = calloc(1, sizeof(struct cvirt_oci_layer));
	if (!layer) {
		goto out;
	}

	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir) {
		template = calloc(strlen(tmpdir) + strlen(LAYER_FILE_TEMPLATE) + 1, sizeof(char));
		if (!template) {
			free(layer);
			layer = NULL;
			goto out;
		}
		strcpy(template, tmpdir);
		strcat(template, LAYER_FILE_TEMPLATE);
	} else {
		template = strdup("/tmp" LAYER_FILE_TEMPLATE);
		if (!template) {
			free(layer);
			layer = NULL;
			goto out;
		}
	}
	layer->fd = mkstemp(template);
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
	res = archive_write_add_filter_zstd(layer->archive);
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
	free(template);
	return layer;
}

int cvirt_oci_layer_close(struct cvirt_oci_layer *layer) {
	archive_write_close(layer->archive);
	archive_write_free(layer->archive);
	close(layer->fd);
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
	return 0;
}
