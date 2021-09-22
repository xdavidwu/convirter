#include "guestfs.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mounts_cmp(const void *a, const void *b) {
	return strcmp(*(const char **)a, *(const char **)b);
}

guestfs_h *create_guestfs_mount_first_linux(const char *image,
		char ***succeeded_mounts) {
	guestfs_h *result = guestfs_create();
	int res;
	if (!result) {
		fprintf(stderr, "Cannot create libguestfs handle\n");
		return NULL;
	}
	res = guestfs_add_drive_opts(result, image,
		GUESTFS_ADD_DRIVE_OPTS_READONLY, 1, -1);
	if (res < 0) {
		fprintf(stderr, "Cannot add image %s to libguestfs\n", image);
		goto err_created;
	}
	res = guestfs_launch(result);
	if (res < 0) {
		fprintf(stderr, "Cannot launch libguestfs\n");
		goto err_launched;
	}

	char **roots = guestfs_inspect_os(result);
	char *target_root = NULL;
	if (!roots) {
		fprintf(stderr, "No OSes detected\n");
		goto err_launched;
	} else if (!roots[0]) {
		free(roots);
		fprintf(stderr, "No root found\n");
		goto err_launched;
	}
	int i = 0;
	for (; roots[i]; i++) {
		char *type = guestfs_inspect_get_type(result, roots[i]);
		if (!type) {
			free(roots[i]);
			continue;
		}
		if (!strcmp(type, "linux")) {
			target_root = roots[i];
			for (int j = i + 1; roots[j]; j++) {
				free(roots[j]);
			}
			break;
		} else {
			free(roots[i]);
		}
	}
	free(roots);
	if (!target_root) {
		fprintf(stderr, "Cannot find any Linux-based OS\n");
		goto err_launched;
	}

	char **mounts = guestfs_inspect_get_mountpoints(result, target_root);
	free(target_root);
	if (!mounts) {
		goto err_launched;
	} else if (!mounts[0]) {
		free(mounts);
		goto err_launched;
	}
	int sz = 0, succeeded_mounts_index = 0;
	while (mounts[sz += 2]);
	if (succeeded_mounts) {
		*succeeded_mounts = calloc(sz + 1, sizeof(char *));
		if (!*succeeded_mounts) {
			goto err_launched;
		}
	}
	sz /= 2;
	qsort(mounts, sz, sizeof(char *) * 2, mounts_cmp);
	for (int index = 0; mounts[index]; index += 2) {
		if (mounts[index][1] != '\0' &&
				!guestfs_is_dir(result, mounts[index])) {
			res = guestfs_mkdir_p(result, mounts[index]);
			if (res < 0) {
				fprintf(stderr, "Warning: mountpoint %s setup failed.\n",
					mounts[index]);
			}
		}
		res = guestfs_mount(result, mounts[index + 1], mounts[index]);
		if (res < 0) {
			fprintf(stderr, "Warning: mount %s at %s failed.\n",
				mounts[index + 1], mounts[index]);
			free(mounts[index]);
			free(mounts[index + 1]);
		} else if (succeeded_mounts) {
			(*succeeded_mounts)[succeeded_mounts_index++] = mounts[index];
			(*succeeded_mounts)[succeeded_mounts_index++] = mounts[index + 1];
		} else {
			free(mounts[index]);
			free(mounts[index + 1]);
		}
	}
	free(mounts);

	return result;

err_launched:
	guestfs_shutdown(result);
err_created:
	guestfs_close(result);
	return NULL;
}

