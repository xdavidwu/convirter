#ifndef GOARCH_H
#define GOARCH_H

#include <endian.h>

/*
 * See https://golang.org/doc/install/source#environment for GOARCH list
 * https://wiki.debian.org/ArchitectureSpecificsMemo for macros
 */

#if	defined(__x86_64__) && defined(__LP64__)
#define	NATIVE_GOARCH	"amd64"
#elif	defined(__i386__)
#define	NATIVE_GOARCH	"386"
#elif	defined(__aarch64__) && defined(__LP64__)
#define	NATIVE_GOARCH	"arm64"
#elif	defined(__arm__)
#define	NATIVE_GOARCH	"arm"
#elif	defined(__powerpc__) && defined(__ppc64__) && (__BYTE_ORDER == __LITTLE_ENDIAN)
#define	NATIVE_GOARCH	"ppc64le"
#elif	defined(__powerpc__) && defined(__powerpc64__) && (__BYTE_ORDER == __BIG_ENDIAN)
#define	NATIVE_GOARCH	"ppc64"
#elif	defined(__mips__) && defined(_MIPSEL) && (_MIPS_SIM == _ABI64)
#define	NATIVE_GOARCH	"mips64le"
#elif	defined(__mips__) && defined(_MIPSEB) && (_MIPS_SIM == _ABI64)
#define	NATIVE_GOARCH	"mips64"
#elif	defined(__mips__) && defined(_MIPSEL) && (_MIPS_SIM == _ABIO32)
#define	NATIVE_GOARCH	"mipsle"
#elif	defined(__mips__) && defined(_MIPSEB) && (_MIPS_SIM == _ABIO32)
#define	NATIVE_GOARCH	"mips"
#elif	defined(__s390x__)
#define	NATIVE_GOARCH	"s390x"
#elif	defined(__riscv) && (__riscv_xlen == 64)
#define	NATIVE_GOARCH	"riscv64"
#endif

#endif
