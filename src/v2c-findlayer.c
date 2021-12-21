#define _GNU_SOURCE
#include <assert.h>
#include <convirter/io/entry.h>
#include <convirter/io/xattr.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <gcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../common/guestfs.h"

static const int pre_hash_len = 15;

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

static void pre_hash(struct cvirt_io_entry *tree, char *path, int name_loc,
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

static void free_pre_hash(struct cvirt_io_entry *tree) {
	if (S_ISREG(tree->inode->stat.st_mode)) {
		free(tree->userdata);
	} else if (S_ISDIR(tree->inode->stat.st_mode)) {
		for (int i = 0; i < tree->inode->children_len; i++) {
			free_pre_hash(&tree->inode->children[i]);
		}
	}
}

static size_t estimate_reuse_by_filter(struct cvirt_io_entry *tree,
		uint8_t *filter, int log2m, int k, char *path, int name_loc,
		gcry_md_hd_t gcry) {
	if (S_ISREG(tree->inode->stat.st_mode)) {
		strcpy(&path[name_loc], tree->name);
		for (int i = 0; i < k; i++) {
			uint32_t hash = (i < pre_hash_len) ?
				((uint32_t *)tree->userdata)[i] :
				entry_hash(path, tree, i, gcry);
			uint32_t index = hash << (32 - log2m) >> (32 - log2m);
			if (!(filter[index >> 3] & 1 << (index & 0x7))) {
				return 0;
			}
		}
		return tree->inode->stat.st_size;
	} else if (S_ISDIR(tree->inode->stat.st_mode)) {
		int new_name_loc = name_loc;
		if (strcmp(tree->name, "/")) {
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

static int64_t estimate_reuse(struct cvirt_io_entry *tree, int dirfd,
		char *filter, char *pathbuf, gcry_md_hd_t gcry) {
	int fd = openat(dirfd, filter, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "Failed to open %s: %s\n", filter,
			strerror(errno));
		return -errno;
	}

	struct stat stat;
	if (fstat(fd, &stat) < 0) {
		fprintf(stderr, "Failed to fstat %s: %s\n", filter,
			strerror(errno));
		close(fd);
		return -errno;
	}
	if (!S_ISREG(stat.st_mode)) {
		// skip
		close(fd);
		return 0;
	}
	off_t sz = stat.st_size; // 2^log2m+1
	if (!(sz & 1)) {
		return -EINVAL;
	}
	int log2m = 0;
	while (sz != 1) {
		sz >>= 1;
		log2m++;
		if ((sz & 1) && sz != 1) {
			return -EINVAL;
		}
	}
	log2m += 3;

	uint8_t *buf = calloc(stat.st_size, 1);
	assert(buf);
	if (read(fd, buf, stat.st_size) < 0) {
		fprintf(stderr, "Failed to read %s: %s\n", filter,
			strerror(errno));
		free(buf);
		close(fd);
		return -errno;
	}

	int64_t res = estimate_reuse_by_filter(tree, &buf[1], log2m, buf[0],
		pathbuf, 0, gcry);

	free(buf);
	close(fd);
	return res;
}

static int scandir_filters(const struct dirent *a) {
	if (a->d_type != DT_REG && a->d_type != DT_UNKNOWN) {
		return 0;
	}

	int l = strlen(a->d_name);
	// *.filter
	if (l < 8) {
		return 0;
	}
	if (strcmp(&a->d_name[l - 7], ".filter")) {
		return 0;
	}
	return 1;
}

static int scandir_dirs(const struct dirent *a) {
	if (a->d_type != DT_DIR && a->d_type != DT_UNKNOWN) {
		return 0;
	}
	if (!strcmp(a->d_name, ".") || !strcmp(a->d_name, "..")) {
		return 0;
	}
	return 1;
}

static void find_filters(struct cvirt_io_entry *tree, int dirfd,
		const char *path, char *pathbuf, gcry_md_hd_t gcry) {
	struct dirent **namelist;
	int n = scandirat(dirfd, path, &namelist, scandir_filters, alphasort);
	int fd = openat(dirfd, path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "Failed to open %s: %s\n", path,
			strerror(errno));
		return;
	}
	for (int a = 0; a < n; a++) {
		printf("%s: %ld\n", namelist[a]->d_name, estimate_reuse(tree,
			fd, namelist[a]->d_name, pathbuf, gcry));
		free(namelist[a]);
	}
	free(namelist);
	n = scandirat(dirfd, path, &namelist, scandir_dirs, alphasort);
	for (int a = 0; a < n; a++) {
		find_filters(tree, fd, namelist[a]->d_name, pathbuf, gcry);
		free(namelist[a]);
	}
	free(namelist);
	close(fd);
}

#include <time.h>
int main(int argc, char *argv[]) {
	if (argc != 2) {
		return EXIT_FAILURE;
	}
	clock_t old = clock(), new;
	guestfs_h *guestfs = create_guestfs_mount_first_linux(argv[1], NULL);
	new = clock();
	printf("%ld\n", new - old);
	old = new;
	struct cvirt_io_entry *tree = cvirt_io_tree_from_guestfs(guestfs,
		CVIRT_IO_TREE_CHECKSUM | CVIRT_IO_TREE_GUESTFS_BTRFS_SKIP_SNAPSHOTS);
	new = clock();
	printf("%ld\n", new - old);
	old = new;
	int max_len = max_filename_length(tree);
	char path_buffer[max_len + 1];
	gcry_md_hd_t gcry;
	gcry_md_open(&gcry, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC);
	pre_hash(tree, path_buffer, 0, gcry);
	new = clock();
	printf("%ld\n", new - old);
	old = new;

	find_filters(tree, AT_FDCWD, ".", path_buffer, gcry);
	new = clock();
	printf("%ld\n", new - old);
	old = new;

	free_pre_hash(tree);
	gcry_md_close(gcry);
	cvirt_io_tree_destroy(tree);
	return 0;
}

