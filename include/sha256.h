#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>

char *sha256sum_from_file(const char *path);

char *sha256sum_from_mem(const char *buf, size_t sz);

#endif
