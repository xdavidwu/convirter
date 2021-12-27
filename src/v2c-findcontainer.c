#define _GNU_SOURCE
#include <assert.h>
#include <convirter/mtree/entry.h>
#include <convirter/mtree/xattr.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <gcrypt.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../common/guestfs.h"

static const int pre_hash_len = 15;

struct findlayer_result {
	char *image;
	size_t estimated_reuse;
};

static struct findlayer_result *output = NULL;
static int output_initial_size = 128;
static int output_size = 0;
static int output_idx = 0;

struct findlayer_config {
	bool best_image_only;
	const char *data;
	bool keep_btrfs_snapshots;
};

static struct findlayer_config config = {0};

static const char usage[] = "\
Usage: %s [OPTION]... INPUT\n\
Find the best container image for a VM image for v2c layer reuse.\n\
\n\
  -b, --best-only             Print only the best container image name, instead\n\
                              of all considered image names and estimated reused\n\
                              bytes\n\
  -d, --data=DIR              Use DIR as data directory instead of .\n\
      --keep-btrfs-snapshots  Do not try to ignore btrfs snapshots\n";


static const struct option long_options[] = {
	{"best-only",			no_argument,	NULL,	'b'},
	{"data",		required_argument,	NULL,	'd'},
	{"keep-btrfs-snapshots",	no_argument,	NULL,	1},
	{0},
};

static int parse_options(struct findlayer_config *config, int argc, char *argv[]) {
	int opt;
	while ((opt = getopt_long(argc, argv, "bd:", long_options, NULL)) != -1) {
		switch (opt) {
		case '?':
			return -EINVAL;
		case 'b':
			config->best_image_only = true;
			break;
		case 'd':
			config->data = optarg;
			break;
		case 1:
			config->keep_btrfs_snapshots = true;
			break;
		}
	}
	return 0;
}

static uint32_t entry_hash(const char *path, struct cvirt_mtree_entry *entry,
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

static int max_filename_length(struct cvirt_mtree_entry *tree) {
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

static void pre_hash(struct cvirt_mtree_entry *tree, char *path, int name_loc,
		gcry_md_hd_t gcry) {
	if (S_ISREG(tree->inode->stat.st_mode)) {
		strcpy(&path[name_loc], tree->name);
		tree->userdata = calloc(pre_hash_len, sizeof(uint32_t));
		assert(tree->userdata);
		for (int i = 0; i < pre_hash_len; i++) {
			((uint32_t *)tree->userdata)[i] = entry_hash(path, tree, i, gcry);
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
			pre_hash(&tree->inode->children[i], path,
				new_name_loc, gcry);
		}
	}
}

static void free_pre_hash(struct cvirt_mtree_entry *tree) {
	if (S_ISREG(tree->inode->stat.st_mode)) {
		free(tree->userdata);
	} else if (S_ISDIR(tree->inode->stat.st_mode)) {
		for (int i = 0; i < tree->inode->children_len; i++) {
			free_pre_hash(&tree->inode->children[i]);
		}
	}
}

static const size_t ustar_logical_record_size = 512;

static size_t estimate_reuse_by_filter(struct cvirt_mtree_entry *tree,
		uint8_t *filter, int log2m, int k, char *path, int name_loc,
		gcry_md_hd_t gcry) {
	if (S_ISREG(tree->inode->stat.st_mode)) {
		if (k > pre_hash_len) {
			strcpy(&path[name_loc], tree->name);
		}
		for (int i = 0; i < k; i++) {
			uint32_t hash = (i < pre_hash_len) ?
				((uint32_t *)tree->userdata)[i] :
				entry_hash(path, tree, i, gcry);
			uint32_t index = hash << (32 - log2m) >> (32 - log2m);
			if (!(filter[index >> 3] & 1 << (index & 0x7))) {
				return 0;
			}
		}
		// required data record + 1 header record
		return (tree->inode->stat.st_size + ustar_logical_record_size - 1) /
			ustar_logical_record_size * ustar_logical_record_size +
			ustar_logical_record_size;
	} else if (S_ISDIR(tree->inode->stat.st_mode)) {
		int new_name_loc = name_loc;
		if (strcmp(tree->name, "/") && k > pre_hash_len) {
			int l = strlen(tree->name);
			strcpy(&path[name_loc], tree->name);
			path[name_loc + l] = '/';
			new_name_loc += l + 1;
		}
		size_t sum = 0;
		for (int i = 0; i < tree->inode->children_len; i++) {
			sum += estimate_reuse_by_filter(
				&tree->inode->children[i], filter, log2m, k,
				path, new_name_loc, gcry);
		}
		return sum;
	}
	return 0;
}

static void find_filters_fts(struct cvirt_mtree_entry *tree, char *pathbuf, gcry_md_hd_t gcry) {
	char *paths[] = {".", NULL};
	FTS *ftsp = fts_open(paths, FTS_LOGICAL, NULL);
	if (!ftsp) {
		perror("fts_open");
		exit(EXIT_FAILURE);
	}

	FTSENT *ftsent = NULL;
	while ((ftsent = fts_read(ftsp))) {
		if (ftsent->fts_info == FTS_F) {
			if (ftsent->fts_namelen > 7 &&
					!strcmp(&ftsent->fts_name[ftsent->fts_namelen - 7], ".filter")) {
				off_t sz = ftsent->fts_statp->st_size; // 2^log2m+1
				if (!(sz & 1)) {
					continue;
				}
				int log2m = 0;
				while (sz != 1) {
					sz >>= 1;
					log2m++;
					if ((sz & 1) && sz != 1) {
						continue;
					}
				}
				log2m += 3;

				int fd = open(ftsent->fts_accpath, O_RDONLY | O_CLOEXEC);
				if (fd < 0) {
					fprintf(stderr, "Failed to open %s: %s\n",
						ftsent->fts_accpath, strerror(errno));
					continue;
				}

				uint8_t *buf = calloc(ftsent->fts_statp->st_size, 1);
				assert(buf);
				if (read(fd, buf, ftsent->fts_statp->st_size) < 0) {
					fprintf(stderr, "Failed to read %s: %s\n",
						ftsent->fts_accpath, strerror(errno));
					free(buf);
					close(fd);
					continue;
				}

				if (output_idx == output_size) {\
					output_size = output_size ? output_size * 2 : output_initial_size;
					output = realloc(output, sizeof(struct findlayer_result) * output_size);
					if (!output) {
						perror("realloc");
						exit(EXIT_FAILURE);
					}
				}

				int image_len = strlen(ftsent->fts_accpath) - 9;
				output[output_idx].image = strndup(
					&ftsent->fts_accpath[2], image_len);
				if (!output[output_idx].image) {
					perror("strdup");
					exit(EXIT_FAILURE);
				}
				bool is_digest = false;
				while (image_len--) {
					if (output[output_idx].image[image_len] == ':') {
						is_digest = true;
					} else if (output[output_idx].image[image_len] == '/') {
						output[output_idx].image[image_len] =
							is_digest ? '@' : ':';
						break;
					}
				}
				output[output_idx++].estimated_reuse =
					estimate_reuse_by_filter(tree, &buf[1],
					log2m, buf[0], pathbuf, 0, gcry);

				free(buf);
				close(fd);
			}
		} // TODO handle others
	}
	if (errno) {
		perror("fts_read");
	}
	fts_close(ftsp);
}

static int output_cmp(const void *a, const void *b) {
	const struct findlayer_result *as = a, *bs = b;
	if (as->estimated_reuse > bs->estimated_reuse) {
		return 1;
	} else if (as->estimated_reuse < bs->estimated_reuse) {
		return -1;
	}
	return strcmp(as->image, bs->image);
}

int main(int argc, char *argv[]) {
	if (parse_options(&config, argc, argv) < 0 || argc - optind != 1) {
		fprintf(stderr, usage, argv[0]);
		exit(EXIT_FAILURE);
	}
	guestfs_h *guestfs = create_guestfs_mount_first_linux(argv[optind], NULL);
	if (!guestfs) {
		exit(EXIT_FAILURE);
	}

	uint32_t flags = CVIRT_MTREE_TREE_CHECKSUM |
		CVIRT_MTREE_TREE_GUESTFS_BTRFS_SKIP_SNAPSHOTS;
	if (config.keep_btrfs_snapshots) {
		flags ^= CVIRT_MTREE_TREE_GUESTFS_BTRFS_SKIP_SNAPSHOTS;
	}

	struct cvirt_mtree_entry *tree = cvirt_mtree_tree_from_guestfs(guestfs, flags);
	int max_len = max_filename_length(tree);
	char path_buffer[max_len + 1];
	gcry_md_hd_t gcry;
	gcry_md_open(&gcry, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC);
	pre_hash(tree, path_buffer, 0, gcry);

	if (config.data) {
		if (chdir(config.data) < 0) {
			perror("chdir");
			exit(EXIT_FAILURE);
		}
	}

	find_filters_fts(tree, path_buffer, gcry);

	qsort(output, output_idx, sizeof (struct findlayer_result), output_cmp);
	if (config.best_image_only && output_idx) {
		puts(output[output_idx - 1].image);
	}
	for (int i = 0; i < output_idx; i++) {
		if (!config.best_image_only) {
			printf("%s: %ld\n", output[i].image, output[i].estimated_reuse);
		}
		free(output[i].image);
	}
	free(output);

	free_pre_hash(tree);
	gcry_md_close(gcry);
	cvirt_mtree_tree_destroy(tree);
	return 0;
}

