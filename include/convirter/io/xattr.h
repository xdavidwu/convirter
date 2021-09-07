#ifndef CVIRT_IO_XATTR
#define CVIRT_IO_XATTR

#include <stddef.h>
#include <stdint.h>

struct cvirt_io_xattr {
	char *name;
	size_t len;
	uint8_t *value;
};

void cvirt_io_xattr_destroy(struct cvirt_io_xattr *xattr);

#endif
