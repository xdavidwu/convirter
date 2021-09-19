#include "hex.h"

#include <stdio.h>

void hex_to_bin(uint8_t *dest, char *src, size_t sz) {
	for (int a = 0; a < sz; a++) {
		unsigned int temp;
		sscanf(&src[a * 2], "%02x", &temp);
		dest[a] = temp;
	}
}

