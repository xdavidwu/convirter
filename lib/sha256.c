#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *sha256sum(const char *path) {
	char *sum = calloc(65, sizeof(char));
	if (!sum) {
		return NULL;
	}

	int l = strlen(path) + 13;
	char *cmd = calloc(l, sizeof(char));
	if (!cmd) {
		free(sum);
		return NULL;
	}
	sprintf(cmd, "sha256sum \"%s\"", path);
	FILE *p = popen(cmd, "r");
	free(cmd);
	if (!p) {
		perror("popen");
		free(sum);
		return NULL;
	}
	if (fscanf(p, "%64s", sum) != 1) {
		fprintf(stderr, "Unable to sha256sum %s\n", path);
		free(sum);
		sum = NULL;
	}
	int ret = pclose(p);
	if (ret != 0) {
		fprintf(stderr, "sha256sum exited with code %d\n", ret);
		free(sum);
		sum = NULL;
	}
	return sum;
}
