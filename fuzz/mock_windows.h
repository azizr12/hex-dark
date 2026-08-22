#ifndef MOCK_WINDOWS_H
#define MOCK_WINDOWS_H

#include <stdint.h>
#include <stdio.h>

/* Mock MSVC-specific 64-bit integer type */
typedef int64_t __int64;

/* Map MSVC file positioning functions to their POSIX equivalents */
#define _fseeki64 fseeko
#define _ftelli64 ftello

#endif