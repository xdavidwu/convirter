#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

char *sha256sum_from_file(const char *path) {
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

char *sha256sum_from_mem(const char *buf, size_t sz) {
	char *sum = calloc(65, sizeof(char));
	if (!sum) {
		return NULL;
	}

	pid_t pid;
	int pipes[4];

	if (pipe(&pipes[0]) < 0) {
		perror("pipe");
		return NULL;
	}
	if (pipe(&pipes[2]) < 0) {
		perror("pipe");
		return NULL;
	}

	pid = fork();
	if (pid < 0) {
		perror("fork");
		return NULL;
	}

	if (pid > 0) {
		close(pipes[0]);
		close(pipes[3]);
		write(pipes[1], buf, sz);
		close(pipes[1]);
	} else {
		close(pipes[1]);
		close(pipes[2]);
		dup2(pipes[0], STDIN_FILENO);
		dup2(pipes[3], STDOUT_FILENO);
		close(pipes[0]);
		close(pipes[3]);
		execlp("sha256sum", "sha256sum", NULL);
		perror("exec");
	}
	int status;
	waitpid(pid, &status, 0);
	FILE *p = fdopen(pipes[2], "r");
	if (fscanf(p, "%64s", sum) != 1) {
		fprintf(stderr, "Unable to sha256sum\n");
		free(sum);
		sum = NULL;
	}
	fclose(p);
	if (WEXITSTATUS(status) != 0) {
		fprintf(stderr, "sha256sum exited with code %d\n", WEXITSTATUS(status));
		free(sum);
		sum = NULL;
	}
	close(pipes[2]);
	return sum;
}
