#include <convirter/io/entry.h>
#include <convirter/oci-r/index.h>
#include <convirter/oci-r/layer.h>
#include <convirter/oci-r/manifest.h>

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void print_tree(struct cvirt_io_entry *entry, int level) {
	for (int i = 0; i < level; i++) {
		putchar('-');
	}
	fputs(entry->name, stdout);
	if (S_ISREG(entry->stat.st_mode)) {
		putchar(' ');
		for (int i = 0; i < 32; i++) {
			printf("%02x", entry->sha256sum[i]);
		}
	}
	putchar('\n');

	if (S_ISDIR(entry->stat.st_mode)) {
		for (int i = 0; i < entry->children_len; i++) {
			print_tree(&entry->children[i], level + 1);
		}
	}
}

int main(int argc, char *argv[]) {
	assert(argc == 2);
	int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	assert(fd != -1);
	struct cvirt_oci_r_index *index = cvirt_oci_r_index_from_archive(fd);
	const char *manifest_digest = cvirt_oci_r_index_get_native_manifest_digest(index);
	struct cvirt_oci_r_manifest *manifest = cvirt_oci_r_manifest_from_archive_blob(fd, manifest_digest);
	// TODO multi-layer
	const char *layer_digest = cvirt_oci_r_manifest_get_layer_digest(manifest, 0);
	struct cvirt_oci_r_layer *layer =
		cvirt_oci_r_layer_from_archive_blob(fd, layer_digest,
		cvirt_oci_r_manifest_get_layer_compression(manifest, 0));
	struct cvirt_io_entry *tree = cvirt_io_tree_from_oci_layer(layer, CVIRT_IO_TREE_CHECKSUM);
	cvirt_oci_r_layer_destroy(layer);
	cvirt_oci_r_manifest_destroy(manifest);
	cvirt_oci_r_index_destroy(index);
	close(fd);

	print_tree(tree, 0);
	cvirt_io_tree_destroy(tree);

	return 0;
}
