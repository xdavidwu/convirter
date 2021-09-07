#include <convirter/io/xattr.h>

#include <stdlib.h>

void cvirt_io_xattr_destroy(struct cvirt_io_xattr *xattr) {
	if (!xattr) {
		return;
	}

	free(xattr->name);
	free(xattr->value);

	free(xattr);
}
