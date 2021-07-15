#include "sha256.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gcrypt.h>

static void bin_to_hex(char *dest, uint8_t *src, size_t sz) {
	for (int a = 0; a < sz; a++) {
		sprintf(&dest[a * 2], "%02x", src[a]);
	}
}

char *sha256sum_from_file(const char *path) {
	char *sum = calloc(65, sizeof(char));
	if (!sum) {
		return NULL;
	}
	char *buf = calloc(1 * 1024 * 1024, sizeof(char));
	if (!buf) {
		free(sum);
		return NULL;
	}
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		free(sum);
		free(buf);
		return NULL;
	}
	gcry_md_hd_t h;
	gcry_md_open(&h, GCRY_MD_SHA256, 0);

	int res;
	while ((res = read(fd, buf, 1 * 1024 * 1024))) {
		gcry_md_write(h, buf, res);
	}

	bin_to_hex(sum, gcry_md_read(h, 0), 32);
	gcry_md_close(h);
	close(fd);
	return sum;
}

char *sha256sum_from_mem(const char *buf, size_t sz) {
	char *sum = calloc(65, sizeof(char));
	if (!sum) {
		return NULL;
	}
	uint8_t *digest = calloc(32, sizeof(uint8_t));
	if (!digest) {
		free(sum);
		return NULL;
	}
	gcry_md_hash_buffer(GCRY_MD_SHA256, digest, buf, sz);
	bin_to_hex(sum, digest, 32);
	free(digest);
	return sum;
}
