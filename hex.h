#ifndef HEX_EDITOR_H
#define HEX_EDITOR_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define WINDOW_SIZE     4096
#define MAX_PATH_LEN    256
#define DEFAULT_TRACKER_CAP 65536

/* ------------------------------------------------------------------ */
/*  Core Data Structures                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    size_t  offset;
    uint8_t original;
    uint8_t modified;
} DirtyByte;

typedef struct {
    DirtyByte *items;
    size_t     count;
    size_t     capacity;
} DirtyTracker;

typedef struct {
    /* File-backed storage */
    FILE   *fp;
    size_t  file_size;

    /* Memory-only storage (no file opened) */
    int      memory_mode;
    uint8_t *mem_buffer;
    size_t   mem_size;
    size_t   mem_capacity;

    /* View / edit state */
    size_t  cursor;
    size_t  view_offset;
    int     bytes_per_row;
    int     edit_mode;        /* 0 = Hex, 1 = Text          */
    int     view_layout;      /* 0 = Hex+ASCII, 1 = ASCII    */
    int     readonly_mode;    /* 0 = overwrite, 1 = readonly */

    /* Sliding read window */
    uint8_t window[WINDOW_SIZE];
    size_t  window_start;
    size_t  window_len;

    char         filename[MAX_PATH_LEN];
    DirtyTracker tracker;

    /* Selection (SIZE_MAX = no selection) */
    size_t selection_start;
    size_t selection_end;
} HexEditor;

/* ------------------------------------------------------------------ */
/*  Engine API  (hex.c)                                                */
/* ------------------------------------------------------------------ */

void    init_tracker(DirtyTracker *t, size_t initial_cap);
void    load_window(HexEditor *ed, size_t target_offset);
uint8_t get_byte(HexEditor *ed, size_t offset);
void    set_byte(HexEditor *ed, size_t offset, uint8_t value);
int     save_dirty(HexEditor *ed);
int     init_file(HexEditor *ed, const char *filename);
void    init_memory_mode(HexEditor *ed);
void    cleanup_editor(HexEditor *ed);
size_t  get_effective_size(HexEditor *ed);

/* Selection helpers */
void   clear_selection(HexEditor *ed);
int    has_selection(HexEditor *ed);
size_t sel_min(HexEditor *ed);
size_t sel_max(HexEditor *ed);

#endif /* HEX_EDITOR_H */