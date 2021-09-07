#include <convirter/io/entry.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static int mounts_cmp(const void *a, const void *b) {
	return strcmp(*(const char **)a, *(const char **)b);
}

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
	guestfs_h *guestfs = guestfs_create();
	assert(guestfs);
	assert(guestfs_add_drive_opts(guestfs, argv[1],
		GUESTFS_ADD_DRIVE_OPTS_READONLY, 1, -1) >= 0);
	assert(guestfs_launch(guestfs) >= 0);

	char **roots = guestfs_inspect_os(guestfs);
	char *target_root = NULL;
	if (!roots) {
		exit(EXIT_FAILURE);
	} else if (!roots[0]) {
		fprintf(stderr, "No root found.\n");
		exit(EXIT_FAILURE);
	}
	int i = 0;
	for (; roots[i]; i++) {
		char *type = guestfs_inspect_get_type(guestfs, roots[i]);
		if (!type) {
			free(roots[i]);
			continue;
		}
		if (!strcmp(type, "linux")) {
			target_root = strdup(roots[i]);
			assert(target_root);
			break;
		}
		free(roots[i]);
	}
	for (; roots[i]; i++) {
		free(roots[i]);
	}
	free(roots);
	if (!target_root) {
		fprintf(stderr, "Cannot find any Linux-based OS\n");
		exit(EXIT_FAILURE);
	}

	char **mounts = guestfs_inspect_get_mountpoints(guestfs, target_root);
	free(target_root);
	if (!mounts || !mounts[0]) {
		exit(EXIT_FAILURE);
	}
	int sz = 0, res = 0;
	while (mounts[sz += 2]);
	sz /= 2;
	qsort(mounts, sz, sizeof(char *) * 2, mounts_cmp);
	for (int index = 0; mounts[index]; index += 2) {
		if (mounts[index][1] != '\0' &&
				!guestfs_is_dir(guestfs, mounts[index])) {
			res = guestfs_mkdir_p(guestfs, mounts[index]);
			if (res < 0) {
				fprintf(stderr, "Warning: mountpoint %s setup failed.\n",
					mounts[index]);
			}
		}
		res = guestfs_mount(guestfs, mounts[index + 1], mounts[index]);
		if (res < 0) {
			fprintf(stderr, "Warning: mount %s at %s failed.\n",
				mounts[index + 1], mounts[index]);
			continue;
		}
	}

	struct cvirt_io_entry *tree = cvirt_io_tree_from_guestfs(guestfs);
	print_tree(tree, 0);
	cvirt_io_tree_destroy(tree);

	return 0;
}
