#ifndef COMPRESSOR_H
#define COMPRESSOR_H

enum compression {
	COMPRESSION_ZSTD,
	COMPRESSION_GZIP,
};

int compress(int fd_in, int fd_out, enum compression compression, int level);

#endif
