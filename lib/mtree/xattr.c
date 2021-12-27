#include <convirter/mtree/xattr.h>

#include <stdlib.h>

void cvirt_mtree_xattr_destroy(struct cvirt_mtree_xattr *xattr) {
	if (!xattr) {
		return;
	}

	free(xattr->name);
	free(xattr->value);

	free(xattr);
}
