#ifndef ARCHIVE_UTILS_H
#define ARCHIVE_UTILS_H

#include <archive_entry.h>

struct archive *archive_from_file_and_seek(const char *path, const char *name,
	struct archive_entry **entry);

struct json_object *json_from_archive(const char *path, const char *name);

char *digest_to_name(const char *digest);

#endif
