#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* The mock header is injected via compiler flag, but we include hex.h normally */
#include "hex.h"

/* Helper to safely read a size_t from fuzz data */
static const uint8_t* read_size_t(const uint8_t* data, size_t size, size_t* out) {
    if (size < sizeof(size_t)) return data;
    memcpy(out, data, sizeof(size_t));
    return data + sizeof(size_t);
}

/* Helper to safely read a uint8_t from fuzz data */
static const uint8_t* read_u8(const uint8_t* data, size_t size, uint8_t* out) {
    if (size < 1) return data;
    *out = data[0];
    return data + 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 16) return 0;

    /* Split data: 1/3 for initial file content, 2/3 for operations */
    size_t initial_file_size = size / 3;
    if (initial_file_size > 1024 * 1024) initial_file_size = 1024 * 1024; /* Cap at 1MB */

    const uint8_t *file_data = data;
    const uint8_t *ops_data = data + initial_file_size;
    size_t ops_size = size - initial_file_size;

    /* ================================================================= */
    /* Test 1: Memory Mode                                               */
    /* ================================================================= */
    {
        HexEditor ed_mem;
        memset(&ed_mem, 0, sizeof(ed_mem));
        ed_mem.memory_mode = 1;
        ed_mem.readonly_mode = 0;
        ed_mem.bytes_per_row = 16;
        init_tracker(&ed_mem.tracker, 64);

        const uint8_t *ptr = ops_data;
        size_t remaining = ops_size / 2;
        size_t max_ops = remaining / (sizeof(size_t) + 1);
        
        for (size_t i = 0; i < max_ops; i++) {
            size_t offset = 0;
            uint8_t value = 0;
            ptr = read_size_t(ptr, remaining, &offset);
            ptr = read_u8(ptr, remaining, &value);
            
            /* Cap offset to prevent excessive memory allocation in fuzzing */
            if (offset > 1024 * 1024) offset = 1024 * 1024;
            
            set_byte(&ed_mem, offset, value);
            (void)get_byte(&ed_mem, offset);
        }
        cleanup_editor(&ed_mem);
    }

    /* ================================================================= */
    /* Test 2: File Mode (using fmemopen for safe, in-memory simulation) */
    /* ================================================================= */
    {
        /* fmemopen requires a mutable buffer */
        uint8_t *mutable_file_data = malloc(initial_file_size + 1);
        if (!mutable_file_data) return 0;
        memcpy(mutable_file_data, file_data, initial_file_size);
        mutable_file_data[initial_file_size] = '\0'; /* Null-terminate for safety */
        
        FILE *fp = fmemopen(mutable_file_data, initial_file_size, "r+");
        if (!fp) {
            free(mutable_file_data);
            return 0;
        }

        HexEditor ed_file;
        memset(&ed_file, 0, sizeof(ed_file));
        ed_file.fp = fp;
        ed_file.memory_mode = 0;
        ed_file.readonly_mode = 0;
        ed_file.original_file_size = initial_file_size;
        ed_file.file_size = initial_file_size;
        ed_file.bytes_per_row = 16;
        init_tracker(&ed_file.tracker, 64);

        const uint8_t *ptr = ops_data + (ops_size / 2);
        size_t remaining = ops_size - (ops_size / 2);
        size_t max_ops = remaining / (sizeof(size_t) + 1);
        
        for (size_t i = 0; i < max_ops; i++) {
            size_t offset = 0;
            uint8_t value = 0;
            ptr = read_size_t(ptr, remaining, &offset);
            ptr = read_u8(ptr, remaining, &value);
            
            /* Cap offset to prevent massive file extensions in fuzzing */
            if (offset > 1024 * 1024) offset = 1024 * 1024;
            
            set_byte(&ed_file, offset, value);
            (void)get_byte(&ed_file, offset);
        }
        
        /* Attempt to save dirty changes (writes to the fmemopen buffer) */
        (void)save_dirty(&ed_file);
        
        cleanup_editor(&ed_file);
        fclose(fp);
        free(mutable_file_data);
    }

    return 0;
}