#ifndef MOCK_WINDOWS_H
#define MOCK_WINDOWS_H

#include <stdint.h>

// Constants expected by hex.h but defined elsewhere in the Windows build
#define WINDOW_SIZE 65536
#define MAX_PATH_LEN 512
#define DEFAULT_TRACKER_CAP 64

// Mock MSVC-specific types and functions for Linux compilation
typedef int64_t __int64;
#define _fseeki64 fseeko
#define _ftelli64 ftello

#endif
