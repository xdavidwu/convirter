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
	guestfs_set_autosync(result, 0);
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
		goto try_first_filesystem;
	} else if (!roots[0]) {
		free(roots);
		fprintf(stderr, "No root found\n");
		goto try_first_filesystem;
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
		fprintf(stderr, "No mountpoints detected\n");
		goto err_launched;
	} else if (!mounts[0]) {
		free(mounts);
		fprintf(stderr, "No mountpoints detected\n");
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

try_first_filesystem:
	fprintf(stderr, "Trying first mountable filesystem\n");
	char **filesystems = guestfs_list_filesystems(result);
	if (!filesystems) {
		fprintf(stderr, "Cannot found any filesystem\n");
		goto err_launched;
	} else if (!filesystems[0]) {
		fprintf(stderr, "Cannot found any filesystem\n");
		free(filesystems);
		goto err_launched;
	}
	bool mounted = false;
	for (int i = 0; filesystems[i]; i += 2) {
		if (!mounted && strcmp(filesystems[i + 1], "swap") &&
				strcmp(filesystems[i + 1], "unknown")) {
			if (guestfs_mount(result, filesystems[i], "/") == 0) {
				mounted = true;
			}
		}
		free(filesystems[i]);
		free(filesystems[i + 1]);
	}
	free(filesystems);
	if (mounted) {
		return result;
	}
	fprintf(stderr, "Cannot mount first filesystem\n");
err_launched:
	guestfs_shutdown(result);
err_created:
	guestfs_close(result);
	return NULL;
}

guestfs_h *create_qcow2_btrfs_image(const char *path, size_t size) {
	guestfs_h *guestfs = guestfs_create();
	if (!guestfs) {
		fprintf(stderr, "Cannot create libguestfs handle\n");
		return NULL;
	}
	int res = guestfs_disk_create(guestfs, path, "qcow2", size, -1);
	if (res < 0) {
		fprintf(stderr, "Failed creating new disk\n");
		goto err_created;
	}
	res = guestfs_add_drive_opts(guestfs, path, GUESTFS_ADD_DRIVE_OPTS_FORMAT, "qcow2", -1);
	if (res < 0) {
		fprintf(stderr, "Failed adding new disk\n");
		goto err_created;
	}
	res = guestfs_launch(guestfs);
	if (res < 0) {
		fprintf(stderr, "Failed launching guestfs\n");
		goto err_launched;
	}

	char *const required_features[] = {
		"btrfs",
		NULL,
	};
	if (!guestfs_feature_available(guestfs, required_features)) {
		fprintf(stderr, "Required libguestfs feature \"btrfs\" not available\n");
		goto err_launched;
	}

	char *const btrfs_devices[] = {
		"/dev/sda",
		NULL,
	};
	if (guestfs_mkfs_btrfs(guestfs, btrfs_devices, -1) < 0) {
		fprintf(stderr, "Failed to mkfs.btrfs\n");
		goto err_launched;
	}
	if (guestfs_mount(guestfs, "/dev/sda", "/") < 0) {
		fprintf(stderr, "Failed to mount\n");
		goto err_launched;
	}

	return guestfs;
err_launched:
	guestfs_shutdown(guestfs);
err_created:
	guestfs_close(guestfs);
	return NULL;
}
