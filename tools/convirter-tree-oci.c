#include <convirter/io/entry.h>
#include <convirter/oci-r/index.h>
#include <convirter/oci-r/layer.h>
#include <convirter/oci-r/manifest.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void print_tree(struct cvirt_io_entry *entry, int level) {
	for (int i = 0; i < level; i++) {
		putchar('-');
	}
	puts(entry->name);

	if (S_ISDIR(entry->stat.st_mode)) {
		for (int i = 0; i < entry->children_len; i++) {
			print_tree(&entry->children[i], level + 1);
		}
	}
}

int main(int argc, char *argv[]) {
	assert(argc == 2);
	struct cvirt_oci_r_index *index = cvirt_oci_r_index_from_archive(argv[1]);
	const char *manifest_digest = cvirt_oci_r_index_get_native_manifest_digest(index);
	struct cvirt_oci_r_manifest *manifest = cvirt_oci_r_manifest_from_archive_blob(argv[1], manifest_digest);
	// TODO multi-layer
	const char *layer_digest = cvirt_oci_r_manifest_get_layer_digest(manifest, 0);
	struct cvirt_oci_r_layer *layer =
		cvirt_oci_r_layer_from_archive_blob(argv[1], layer_digest,
		cvirt_oci_r_manifest_get_layer_compression(manifest, 0));
	struct cvirt_io_entry *tree = cvirt_io_tree_from_oci_layer(layer, 0);
	cvirt_oci_r_layer_destroy(layer);
	cvirt_oci_r_manifest_destroy(manifest);
	cvirt_oci_r_index_destroy(index);

	print_tree(tree, 0);
	cvirt_io_tree_destroy(tree);

	return 0;
}
