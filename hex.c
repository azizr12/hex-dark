#include "hex.h"

/* ================================================================== */
/*  Dirty Tracker                                                      */
/* ================================================================== */

void init_tracker(DirtyTracker *t, size_t initial_cap)
{
    if (initial_cap < 64) initial_cap = 64;
    t->capacity = initial_cap;
    t->count    = 0;
    t->items    = (DirtyByte *)malloc(t->capacity * sizeof(DirtyByte));
}

/* ================================================================== */
/*  Sliding Window                                                     */
/* ================================================================== */

void load_window(HexEditor *ed, size_t target_offset)
{
    int bpr = ed->bytes_per_row;
    if (bpr < 1) bpr = 16;
    ed->window_start = (target_offset / (size_t)bpr) * (size_t)bpr;
    fseek(ed->fp, (long)ed->window_start, SEEK_SET);
    ed->window_len = fread(ed->window, 1, WINDOW_SIZE, ed->fp);
}

/* ================================================================== */
/*  Byte Access (unified: file-backed or memory)                       */
/* ================================================================== */

size_t get_effective_size(HexEditor *ed)
{
    return ed->memory_mode ? ed->mem_size : ed->file_size;
}

uint8_t get_byte(HexEditor *ed, size_t offset)
{
    if (ed->memory_mode) {
        return (offset < ed->mem_size) ? ed->mem_buffer[offset] : 0;
    }
    if (offset >= ed->file_size) return 0;

    /* Check dirty overlay first */
    for (size_t i = 0; i < ed->tracker.count; i++)
        if (ed->tracker.items[i].offset == offset)
            return ed->tracker.items[i].modified;

    /* Fall back to file window */
    if (offset < ed->window_start ||
        offset >= ed->window_start + ed->window_len)
        load_window(ed, offset);

    return ed->window[offset - ed->window_start];
}

void set_byte(HexEditor *ed, size_t offset, uint8_t value)
{
    if (ed->readonly_mode) return;

    /* ---- Memory mode ---- */
    if (ed->memory_mode) {
        if (offset >= ed->mem_capacity) {
            size_t new_cap = ed->mem_capacity ? ed->mem_capacity * 2 : 4096;
            while (new_cap <= offset) new_cap *= 2;
            ed->mem_buffer = (uint8_t *)realloc(ed->mem_buffer, new_cap);
            memset(ed->mem_buffer + ed->mem_capacity, 0,
                   new_cap - ed->mem_capacity);
            ed->mem_capacity = new_cap;
        }
        if (offset >= ed->mem_size) {
            memset(ed->mem_buffer + ed->mem_size, 0,
                   offset - ed->mem_size);
            ed->mem_size = offset + 1;
        }
        ed->mem_buffer[offset] = value;
        return;
    }

    /* ---- File mode: edit only the dirty overlay. ---- */
    if (offset >= ed->file_size) {
        /*
         * Do NOT extend the physical file here.  The editor's file_size
         * is the logical document size and the new byte lives entirely
         * in the dirty tracker until Save.
         */
        if (ed->tracker.count == ed->tracker.capacity) {
            ed->tracker.capacity *= 2;
            ed->tracker.items = (DirtyByte *)realloc(
                ed->tracker.items,
                ed->tracker.capacity * sizeof(DirtyByte));
        }

        ed->tracker.items[ed->tracker.count].offset   = offset;
        ed->tracker.items[ed->tracker.count].original = 0;
        ed->tracker.items[ed->tracker.count].modified = value;
        ed->tracker.count++;
        ed->file_size = offset + 1;
        return;
    }

    /* Existing byte – check if already tracked */
    for (size_t i = 0; i < ed->tracker.count; i++) {
        if (ed->tracker.items[i].offset == offset) {
            ed->tracker.items[i].modified = value;
            return;
        }
    }

    /* Read true original from file window */
    if (offset < ed->window_start ||
        offset >= ed->window_start + ed->window_len)
        load_window(ed, offset);
    uint8_t true_orig = ed->window[offset - ed->window_start];

    if (ed->tracker.count == ed->tracker.capacity) {
        ed->tracker.capacity *= 2;
        ed->tracker.items = (DirtyByte *)realloc(
            ed->tracker.items,
            ed->tracker.capacity * sizeof(DirtyByte));
    }
    ed->tracker.items[ed->tracker.count].offset   = offset;
    ed->tracker.items[ed->tracker.count].original  = true_orig;
    ed->tracker.items[ed->tracker.count].modified  = value;
    ed->tracker.count++;
}

/* ================================================================== */
/*  Save                                                               */
/* ================================================================== */

int save_dirty(HexEditor *ed)
{
    if (ed->memory_mode) return -1;   /* GUI handles memory-mode save */

    for (size_t i = 0; i < ed->tracker.count; i++) {
        fseek(ed->fp, (long)ed->tracker.items[i].offset, SEEK_SET);
        if (fwrite(&ed->tracker.items[i].modified, 1, 1, ed->fp) != 1)
            return -1;
    }

    fflush(ed->fp);
    ed->tracker.count = 0;
    return 0;
}

/* ================================================================== */
/*  Init / Cleanup                                                     */
/* ================================================================== */

int init_file(HexEditor *ed, const char *filename)
{
    ed->fp = fopen(filename, "r+b");
    if (!ed->fp) {
        ed->fp = fopen(filename, "w+b");
        if (!ed->fp) return -1;
    }
    fseek(ed->fp, 0, SEEK_END);
    ed->file_size = (size_t)ftell(ed->fp);
    fseek(ed->fp, 0, SEEK_SET);

    strncpy(ed->filename, filename, MAX_PATH_LEN - 1);
    ed->filename[MAX_PATH_LEN - 1] = '\0';

    ed->memory_mode   = 0;
    ed->cursor        = 0;
    ed->view_offset   = 0;
    ed->window_start  = 0;
    ed->window_len    = 0;
    ed->selection_start = (size_t)-1;
    ed->selection_end   = (size_t)-1;

    init_tracker(&ed->tracker, DEFAULT_TRACKER_CAP);
    return 0;
}

void init_memory_mode(HexEditor *ed)
{
    memset(ed, 0, sizeof(*ed));
    ed->memory_mode     = 1;
    ed->mem_capacity    = 4096;
    ed->mem_size        = 0;
    ed->mem_buffer      = (uint8_t *)calloc(ed->mem_capacity, 1);
    ed->bytes_per_row   = 16;
    ed->edit_mode       = 0;
    ed->view_layout     = 0;
    ed->readonly_mode   = 0;
    ed->cursor          = 0;
    ed->view_offset     = 0;
    ed->selection_start = (size_t)-1;
    ed->selection_end   = (size_t)-1;
    strncpy(ed->filename, "untitled.bin", MAX_PATH_LEN - 1);
    init_tracker(&ed->tracker, DEFAULT_TRACKER_CAP);
}

void cleanup_editor(HexEditor *ed)
{
    if (ed->fp) { fclose(ed->fp); ed->fp = NULL; }
    if (ed->tracker.items) { free(ed->tracker.items); ed->tracker.items = NULL; }
    if (ed->mem_buffer) { free(ed->mem_buffer); ed->mem_buffer = NULL; }
    ed->tracker.count = 0;
    ed->tracker.capacity = 0;
}

/* ================================================================== */
/*  Selection Helpers                                                  */
/* ================================================================== */

void clear_selection(HexEditor *ed)
{
    ed->selection_start = (size_t)-1;
    ed->selection_end   = (size_t)-1;
}

int has_selection(HexEditor *ed)
{
    return (ed->selection_start != (size_t)-1 &&
            ed->selection_end   != (size_t)-1 &&
            ed->selection_start != ed->selection_end);
}

size_t sel_min(HexEditor *ed)
{
    return (ed->selection_start < ed->selection_end)
               ? ed->selection_start : ed->selection_end;
}

size_t sel_max(HexEditor *ed)
{
    return (ed->selection_start > ed->selection_end)
               ? ed->selection_start : ed->selection_end;
}