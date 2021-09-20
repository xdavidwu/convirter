#ifndef ARCHIVE_UTILS_H
#define ARCHIVE_UTILS_H

#include <archive_entry.h>

struct archive *archive_from_fd_and_seek(int fd, const char *name,
	struct archive_entry **entry);

struct json_object *json_from_archive(int fd, const char *name);

char *digest_to_name(const char *digest);

#endif
