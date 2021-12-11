#include <convirter/io/entry.h>
#include <convirter/io/xattr.h>
#include <convirter/oci-r/index.h>
#include <convirter/oci-r/layer.h>
#include <convirter/oci-r/manifest.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gcrypt.h>

static const double false_positive_rate = 10e-5;

static int count_files(struct cvirt_io_entry *tree) {
	if (S_ISREG(tree->inode->stat.st_mode)) {
		return 1;
	} else if (S_ISDIR(tree->inode->stat.st_mode)) {
		int sum = 0;
		for (int i = 0; i < tree->inode->children_len; i++) {
			sum += count_files(&tree->inode->children[i]);
		}
		return sum;
	}
	return 0;
}

static int max_filename_length(struct cvirt_io_entry *tree) {
	if (S_ISREG(tree->inode->stat.st_mode)) {
		return strlen(tree->name);
	} else if (S_ISDIR(tree->inode->stat.st_mode)) {
		int max = 0;
		for (int i = 0; i < tree->inode->children_len; i++) {
			int l = max_filename_length(&tree->inode->children[i]);
			max = (l > max) ? l : max;
		}
		return max ? max + 1 + strlen(tree->name) : 0;
	}
	return 0;
}

static int bloom_size(int entries, double p) {
	int orig = ceil((entries * log(p)) / log(1 / pow(2, log(2))));
	if (orig < 8) {
		orig = 8;
	}
	int powed = 8;
	int pow = 3;
	while (powed < orig) {
		powed <<= 1;
		pow++;
	}
	return pow;
}

static int bloom_hashs(int size, int entries) {
	return round((size /(double) entries) * log(2));
}

static uint32_t entry_hash(const char *path, struct cvirt_io_entry *entry,
		uint8_t i, gcry_md_hd_t gcry) {
	gcry_md_reset(gcry);
	gcry_md_setkey(gcry, &i, 1);
	gcry_md_write(gcry, path, strlen(path) + 1);
	gcry_md_write(gcry, &entry->inode->stat.st_mode, sizeof(mode_t));
	gcry_md_write(gcry, &entry->inode->stat.st_uid, sizeof(uid_t));
	gcry_md_write(gcry, &entry->inode->stat.st_gid, sizeof(gid_t));
	gcry_md_write(gcry, &entry->inode->stat.st_size, sizeof(off_t));
	gcry_md_write(gcry, &entry->inode->stat.st_mtim.tv_sec, sizeof(time_t));
	gcry_md_write(gcry, entry->inode->sha256sum, 32);
	for (int a = 0; a < entry->inode->xattrs_len; a++) {
		gcry_md_write(gcry, entry->inode->xattrs[a].name,
			strlen(entry->inode->xattrs[a].name) + 1);
		gcry_md_write(gcry, &entry->inode->xattrs[a].len, sizeof(size_t));
		gcry_md_write(gcry, entry->inode->xattrs[a].value,
			entry->inode->xattrs[a].len);
	}
	uint8_t *res = gcry_md_read(gcry, 0);
	uint32_t part;
	memcpy(&part, res, sizeof(uint32_t));
	return part;
}

static void build_filter(struct cvirt_io_entry *tree, uint8_t *filter, int log2m,
		int k, char *path, int name_loc, gcry_md_hd_t gcry) {
	if (S_ISREG(tree->inode->stat.st_mode)) {
		strcpy(&path[name_loc], tree->name);
		for (int i = 0; i < k; i++) {
			uint32_t hash = entry_hash(path, tree, i, gcry);
			uint32_t index = hash << (32 - log2m) >> (32 - log2m);
			filter[index >> 3] |= 1 << (index & 0x7);
		}
	} else if (S_ISDIR(tree->inode->stat.st_mode)) {
		int new_name_loc = name_loc;
		if (strcmp(tree->name, "/")) {
			int l = strlen(tree->name);
			strcpy(&path[name_loc], tree->name);
			path[name_loc + l] = '/';
			new_name_loc += l + 1;
		}
		for (int i = 0; i < tree->inode->children_len; i++) {
			build_filter(&tree->inode->children[i], filter, log2m,
				k, path, new_name_loc, gcry);
		}
	}
}

int main(int argc, char *argv[]) {
	if (argc != 3) {
		return EXIT_FAILURE;
	}

	int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "Failed to open OCI archive: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}
	int fdout = open(argv[2], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if (fdout < 0) {
		close(fd);
		fprintf(stderr, "Failed to create filter: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	struct cvirt_oci_r_index *index = cvirt_oci_r_index_from_archive(fd);
	const char *manifest_digest = cvirt_oci_r_index_get_native_manifest_digest(index);
	struct cvirt_oci_r_manifest *manifest = cvirt_oci_r_manifest_from_archive_blob(fd, manifest_digest);
	const char *layer_digest = cvirt_oci_r_manifest_get_layer_digest(manifest, 0);
	struct cvirt_oci_r_layer *layer =
		cvirt_oci_r_layer_from_archive_blob(fd, layer_digest,
		cvirt_oci_r_manifest_get_layer_compression(manifest, 0));
	struct cvirt_io_entry *tree = cvirt_io_tree_from_oci_layer(layer, CVIRT_IO_TREE_CHECKSUM);
	cvirt_oci_r_layer_destroy(layer);
	int len = cvirt_oci_r_manifest_get_layers_length(manifest);
	for (int i = 1; i < len; i++) {
		struct cvirt_oci_r_layer *layer =
			cvirt_oci_r_layer_from_archive_blob(fd,
			cvirt_oci_r_manifest_get_layer_digest(manifest, i),
			cvirt_oci_r_manifest_get_layer_compression(manifest, i));
		cvirt_io_tree_oci_apply_layer(tree, layer, CVIRT_IO_TREE_CHECKSUM);
		cvirt_oci_r_layer_destroy(layer);
	}
	cvirt_oci_r_manifest_destroy(manifest);
	cvirt_oci_r_index_destroy(index);
	close(fd);

	int entries = count_files(tree);
	int filename_length = max_filename_length(tree);
	int log2m = bloom_size(entries, false_positive_rate);
	uint8_t k = bloom_hashs(1 << log2m, entries);
	uint8_t *bits = calloc(1 << (log2m - 3), sizeof(uint8_t));
	if (!bits) {
		fprintf(stderr, "Unable to allocate %d bytes\n", 1 << (log2m - 3));
		cvirt_io_tree_destroy(tree);
		close(fdout);
		return EXIT_FAILURE;
	}
	char path_buffer[filename_length + 1];
	gcry_md_hd_t gcry;
	gcry_md_open(&gcry, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC);
	build_filter(tree, bits, log2m, k, path_buffer, 0, gcry);
	gcry_md_close(gcry);

	int res = write(fdout, &k, 1);
	int remain = 1 << (log2m - 3), idx = 0;
	while (res >= 0 && remain) {
		res = write(fdout, &bits[idx], remain);
		idx += res;
		remain -= res;
	}
	if (res < 0) {
		fprintf(stderr, "Failed to write to filter: %s\n", strerror(errno));
	}

	free(bits);
	close(fdout);
	cvirt_io_tree_destroy(tree);
	return res < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
