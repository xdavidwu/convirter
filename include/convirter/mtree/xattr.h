#ifndef CVIRT_MTREE_XATTR
#define CVIRT_MTREE_XATTR

#include <stddef.h>
#include <stdint.h>

struct cvirt_mtree_xattr {
	char *name;
	size_t len;
	uint8_t *value;
};

void cvirt_mtree_xattr_destroy(struct cvirt_mtree_xattr *xattr);

#endif
