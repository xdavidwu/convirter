#ifndef XMEM_H
#define XMEM_H

#include <sys/types.h>

void *cvirt_xmalloc(size_t size);
void *cvirt_xcalloc(size_t nmemb, size_t size);
void *cvirt_xrealloc(void *ptr, size_t size);
char *cvirt_xstrdup(const char *s);

#endif
