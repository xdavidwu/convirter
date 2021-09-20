#include <assert.h>
#include <archive.h>
#include <archive_entry.h>
#include <convirter/oci-r/index.h>
#include <convirter/oci-r/layer.h>
#include <convirter/oci-r/manifest.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
	if (argc != 2) {
		exit(EXIT_FAILURE);
	}
	int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	assert(fd != -1);
	struct cvirt_oci_r_index *index = cvirt_oci_r_index_from_archive(fd);
	const char *manifest_digest = cvirt_oci_r_index_get_native_manifest_digest(index);
	puts("manifest:");
	puts(manifest_digest);
	struct cvirt_oci_r_manifest *manifest = cvirt_oci_r_manifest_from_archive_blob(fd, manifest_digest);
	const char *config_digest = cvirt_oci_r_manifest_get_config_digest(manifest);
	puts("config:");
	puts(config_digest);
	puts("layers:");
	int len = cvirt_oci_r_manifest_get_layers_length(manifest);
	for (int i = 0; i < len; i++) {
		const char *layer_digest = cvirt_oci_r_manifest_get_layer_digest(manifest, i);
		puts(layer_digest);
		struct cvirt_oci_r_layer *layer =
			cvirt_oci_r_layer_from_archive_blob(fd, layer_digest,
			cvirt_oci_r_manifest_get_layer_compression(manifest, i));
		struct archive *archive = cvirt_oci_r_layer_get_libarchive(layer);
		struct archive_entry *entry;
		while (archive_read_next_header(archive, &entry) == ARCHIVE_OK) {
			puts(archive_entry_pathname(entry));
		}
		cvirt_oci_r_layer_destroy(layer);
	}
	cvirt_oci_r_manifest_destroy(manifest);
	cvirt_oci_r_index_destroy(index);
	close(fd);
	return 0;
}
