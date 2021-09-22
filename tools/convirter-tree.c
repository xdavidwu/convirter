#include <convirter/io/entry.h>

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../common/guestfs.h"

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
	guestfs_h *guestfs = create_guestfs_mount_first_linux(argv[1], NULL);
	if (!guestfs) {
		exit(EXIT_FAILURE);
	}

	struct cvirt_io_entry *tree = cvirt_io_tree_from_guestfs(guestfs, CVIRT_IO_TREE_CHECKSUM);
	print_tree(tree, 0);
	cvirt_io_tree_destroy(tree);

	return 0;
}
