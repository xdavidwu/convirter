#include "archive-utils.h"
#include "convirter/oci-r/layer.h"
#include "oci-r/layer.h"

#include <archive.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>


int inplace_open(struct archive *archive, void *data) {
	return ARCHIVE_OK;
}

la_ssize_t inplace_read(struct archive *archive, void *data, const void **buf) {
	struct cvirt_oci_r_layer *layer = data;
	*buf = layer->buf;
	return archive_read_data(layer->image_archive, layer->buf, BUFSZ);
}

int inplace_close(struct archive *archive, void *data) {
	struct cvirt_oci_r_layer *layer = data;
	archive_read_free(layer->image_archive);
	layer->image_archive = NULL;
	return ARCHIVE_OK;
}

struct cvirt_oci_r_layer *cvirt_oci_r_layer_from_archive_blob(int fd,
		const char *digest,
		enum cvirt_oci_r_layer_compression compression) {
	struct cvirt_oci_r_layer *layer = calloc(1,
		sizeof(struct cvirt_oci_r_layer));
	if (!layer) {
		return NULL;
	}
	assert(lseek(fd, 0, SEEK_SET) == 0);
	layer->fd = fd;
	layer->name = digest_to_name(digest);
	layer->compression = compression;
	layer->image_archive = archive_from_fd_and_seek(fd, layer->name, NULL);
	if (!layer->image_archive) {
		goto err;
	}
	layer->layer_archive = archive_read_new();
	if (!layer->layer_archive) {
		goto err;
	}
	archive_read_support_format_tar(layer->layer_archive);
	switch (compression) {
	case CVIRT_OCI_R_LAYER_COMPRESSION_GZIP:
		archive_read_support_filter_gzip(layer->layer_archive);
		break;
	case CVIRT_OCI_R_LAYER_COMPRESSION_ZSTD:
		archive_read_support_filter_zstd(layer->layer_archive);
		break;
	case CVIRT_OCI_R_LAYER_COMPRESSION_NONE:
		break;
	}
	int res = archive_read_open(layer->layer_archive, layer, inplace_open,
		inplace_read, inplace_close);
	if (res == ARCHIVE_OK) {
		return layer;
	}
	fprintf(stderr, "cvirt_oci_r_layer_from_archive_blob: open layer archive failed: %s\n",
		archive_error_string(layer->layer_archive));
err:
	archive_read_free(layer->layer_archive);
	archive_read_free(layer->image_archive);
	free(layer);
	return NULL;
}

int cvirt_oci_r_layer_rewind(struct cvirt_oci_r_layer *layer) {
	archive_read_free(layer->layer_archive);
	archive_read_free(layer->image_archive);
	assert(lseek(layer->fd, 0, SEEK_SET) == 0);
	layer->image_archive = archive_from_fd_and_seek(layer->fd, layer->name, NULL);
	if (!layer->image_archive) {
		goto err;
	}
	layer->layer_archive = archive_read_new();
	if (!layer->layer_archive) {
		goto err;
	}
	archive_read_support_format_tar(layer->layer_archive);
	switch (layer->compression) {
	case CVIRT_OCI_R_LAYER_COMPRESSION_GZIP:
		archive_read_support_filter_gzip(layer->layer_archive);
		break;
	case CVIRT_OCI_R_LAYER_COMPRESSION_ZSTD:
		archive_read_support_filter_zstd(layer->layer_archive);
		break;
	case CVIRT_OCI_R_LAYER_COMPRESSION_NONE:
		break;
	}
	int res = archive_read_open(layer->layer_archive, layer, inplace_open,
		inplace_read, inplace_close);
	if (res == ARCHIVE_OK) {
		return 0;
	}
	fprintf(stderr, "cvirt_oci_r_layer_rewind: open layer archive failed: %s\n",
		archive_error_string(layer->layer_archive));
err:
	archive_read_free(layer->layer_archive);
	archive_read_free(layer->image_archive);
	free(layer);
	return -1;
}

struct archive *cvirt_oci_r_layer_get_libarchive(struct cvirt_oci_r_layer *layer) {
	return layer->layer_archive;
}

void cvirt_oci_r_layer_destroy(struct cvirt_oci_r_layer *layer) {
	archive_read_free(layer->layer_archive);
	archive_read_free(layer->image_archive);
	free(layer->name);
	free(layer);
}
