#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *cvirt_xmalloc(size_t size) {
	void *res = malloc(size);
	if (!res && size) {
		fprintf(stderr, "malloc(%zu) failed", size);
		exit(EXIT_FAILURE);
	}
	return res;
}

void *cvirt_xcalloc(size_t nmemb, size_t size) {
	void *res = calloc(nmemb, size);
	if (!res && (nmemb * size)) {
		fprintf(stderr, "calloc(%zu, %zu) failed", nmemb, size);
		exit(EXIT_FAILURE);
	}
	return res;
}

void *cvirt_xrealloc(void *ptr, size_t size) {
	void *res = realloc(ptr, size);
	if (!res && size) {
		fprintf(stderr, "realloc(%zu) failed", size);
		exit(EXIT_FAILURE);
	}
	return res;
}

char *cvirt_xstrdup(const char *s) {
	char *res = strdup(s);
	if (!res) {
		fprintf(stderr, "strdup() failed");
		exit(EXIT_FAILURE);
	}
	return res;
}

char *cvirt_xstrndup(const char *s, size_t size) {
	char *res = strndup(s, size);
	if (!res) {
		fprintf(stderr, "strndup() failed");
		exit(EXIT_FAILURE);
	}
	return res;
}
