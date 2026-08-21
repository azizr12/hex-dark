#include "hex.h"

/* ================================================================== */
/* Dirty Tracker                                                       */
/* ================================================================== */

static int tracker_reserve(DirtyTracker *t, size_t needed)
{
    if (needed <= t->capacity) return 0;

    size_t capacity = t->capacity ? t->capacity : 64;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) return -1;
        capacity *= 2;
    }

    DirtyByte *items = (DirtyByte *)realloc(
        t->items, capacity * sizeof(*items));
    if (!items) return -1;

    t->items = items;
    t->capacity = capacity;
    return 0;
}

void init_tracker(DirtyTracker *t, size_t initial_cap)
{
    memset(t, 0, sizeof(*t));
    if (initial_cap < 64) initial_cap = 64;

    t->capacity = initial_cap;
    t->items = (DirtyByte *)malloc(t->capacity * sizeof(*t->items));
    if (!t->items)
        t->capacity = 0;
}

static DirtyByte *find_dirty(HexEditor *ed, size_t offset)
{
    for (size_t i = 0; i < ed->tracker.count; ++i) {
        if (ed->tracker.items[i].offset == offset)
            return &ed->tracker.items[i];
    }
    return NULL;
}

static int track_byte(HexEditor *ed,
                      size_t offset,
                      uint8_t original,
                      uint8_t modified)
{
    DirtyByte *existing = find_dirty(ed, offset);
    if (existing) {
        existing->modified = modified;
        return 0;
    }

    if (tracker_reserve(&ed->tracker, ed->tracker.count + 1) != 0)
        return -1;

    ed->tracker.items[ed->tracker.count].offset = offset;
    ed->tracker.items[ed->tracker.count].original = original;
    ed->tracker.items[ed->tracker.count].modified = modified;
    ++ed->tracker.count;
    return 0;
}

/* ================================================================== */
/* Sliding Window                                                      */
/* ================================================================== */

void load_window(HexEditor *ed, size_t target_offset)
{
    if (!ed->fp) {
        ed->window_start = target_offset;
        ed->window_len = 0;
        return;
    }

    int bpr = ed->bytes_per_row;
    if (bpr < 1) bpr = 16;

    ed->window_start = (target_offset / (size_t)bpr) * (size_t)bpr;

    if (_fseeki64(ed->fp, (__int64)ed->window_start, SEEK_SET) != 0) {
        ed->window_len = 0;
        return;
    }

    ed->window_len = fread(ed->window, 1, WINDOW_SIZE, ed->fp);
}

/* ================================================================== */
/* Byte Access                                                         */
/* ================================================================== */

size_t get_effective_size(HexEditor *ed)
{
    return ed->memory_mode ? ed->mem_size : ed->file_size;
}

uint8_t get_byte(HexEditor *ed, size_t offset)
{
    if (ed->memory_mode)
        return offset < ed->mem_size ? ed->mem_buffer[offset] : 0;

    if (offset >= ed->file_size)
        return 0;

    /* Unsaved edits always take precedence over the physical file. */
    DirtyByte *dirty = find_dirty(ed, offset);
    if (dirty)
        return dirty->modified;

    /* Bytes beyond the original EOF are logically zero until explicitly
       modified, and remain entirely in memory until Save. */
    if (offset >= ed->original_file_size)
        return 0;

    if (offset < ed->window_start ||
        offset >= ed->window_start + ed->window_len)
        load_window(ed, offset);

    if (offset < ed->window_start ||
        offset >= ed->window_start + ed->window_len)
        return 0;

    return ed->window[offset - ed->window_start];
}

void set_byte(HexEditor *ed, size_t offset, uint8_t value)
{
    if (ed->readonly_mode)
        return;

    /* -------------------------------------------------------------- */
    /* Memory mode                                                     */
    /* -------------------------------------------------------------- */
    if (ed->memory_mode) {
        if (offset >= ed->mem_capacity) {
            size_t new_capacity = ed->mem_capacity
                ? ed->mem_capacity * 2 : 4096;

            while (new_capacity <= offset) {
                if (new_capacity > SIZE_MAX / 2)
                    return;
                new_capacity *= 2;
            }

            uint8_t *buffer = (uint8_t *)realloc(
                ed->mem_buffer, new_capacity);
            if (!buffer)
                return;

            memset(buffer + ed->mem_capacity, 0,
                   new_capacity - ed->mem_capacity);
            ed->mem_buffer = buffer;
            ed->mem_capacity = new_capacity;
        }

        if (offset >= ed->mem_size) {
            memset(ed->mem_buffer + ed->mem_size, 0,
                   offset - ed->mem_size);
            ed->mem_size = offset + 1;
        }

        ed->mem_buffer[offset] = value;
        return;
    }

    /* -------------------------------------------------------------- */
    /* File mode                                                       */
    /* -------------------------------------------------------------- */

    /* Capture the original byte before changing the overlay. */
    uint8_t original = 0;
    if (offset < ed->original_file_size)
        original = get_byte(ed, offset);

    /* file_size is the logical document size. It may grow beyond the
       physical file, but the physical file is never touched here. */
    if (offset >= ed->file_size)
        ed->file_size = offset + 1;

    if (track_byte(ed, offset, original, value) != 0)
        return;
}

/* ================================================================== */
/* Save                                                                 */
/* ================================================================== */

int save_dirty(HexEditor *ed)
{
    if (ed->memory_mode || !ed->fp)
        return -1;

    if (ed->tracker.count == 0 &&
        ed->file_size == ed->original_file_size)
        return 0;

    /* First extend the physical file with zeros if the logical document
       grew. This is done only during Save. */
    if (ed->file_size > ed->original_file_size) {
        if (_fseeki64(ed->fp, (__int64)ed->original_file_size, SEEK_SET) != 0)
            return -1;

        size_t remaining = ed->file_size - ed->original_file_size;
        uint8_t zeros[4096] = {0};

        while (remaining > 0) {
            size_t chunk = remaining < sizeof(zeros)
                ? remaining : sizeof(zeros);

            if (fwrite(zeros, 1, chunk, ed->fp) != chunk)
                return -1;

            remaining -= chunk;
        }
    }

    /* Write every changed byte. */
    for (size_t i = 0; i < ed->tracker.count; ++i) {
        DirtyByte *dirty = &ed->tracker.items[i];

        if (_fseeki64(ed->fp, (__int64)dirty->offset, SEEK_SET) != 0)
            return -1;

        if (fwrite(&dirty->modified, 1, 1, ed->fp) != 1)
            return -1;
    }

    if (fflush(ed->fp) != 0)
        return -1;

    /* Only after every write succeeds is the document considered clean. */
    ed->original_file_size = ed->file_size;
    ed->tracker.count = 0;
    return 0;
}

/* ================================================================== */
/* Init / Cleanup                                                      */
/* ================================================================== */

int init_file(HexEditor *ed, const char *filename)
{
    ed->fp = fopen(filename, "r+b");
    if (!ed->fp) {
        ed->fp = fopen(filename, "w+b");
        if (!ed->fp)
            return -1;
    }

    if (_fseeki64(ed->fp, 0, SEEK_END) != 0) {
        fclose(ed->fp);
        ed->fp = NULL;
        return -1;
    }

    __int64 end = _ftelli64(ed->fp);
    if (end < 0 || (uintmax_t)end > (uintmax_t)SIZE_MAX) {
        fclose(ed->fp);
        ed->fp = NULL;
        return -1;
    }

    ed->file_size = (size_t)end;
    ed->original_file_size = ed->file_size;

    if (_fseeki64(ed->fp, 0, SEEK_SET) != 0) {
        fclose(ed->fp);
        ed->fp = NULL;
        return -1;
    }

    strncpy(ed->filename, filename, MAX_PATH_LEN - 1);
    ed->filename[MAX_PATH_LEN - 1] = '\0';

    ed->memory_mode = 0;
    ed->cursor = 0;
    ed->view_offset = 0;
    ed->window_start = 0;
    ed->window_len = 0;
    ed->selection_start = (size_t)-1;
    ed->selection_end = (size_t)-1;
    ed->selection_origin = SELECTION_NONE;

    init_tracker(&ed->tracker, DEFAULT_TRACKER_CAP);
    if (!ed->tracker.items)
        return -1;

    return 0;
}

void init_memory_mode(HexEditor *ed)
{
    memset(ed, 0, sizeof(*ed));

    ed->memory_mode = 1;
    ed->mem_capacity = 4096;
    ed->mem_size = 0;
    ed->mem_buffer = (uint8_t *)calloc(ed->mem_capacity, 1);
    ed->bytes_per_row = 16;
    ed->edit_mode = 0;
    ed->view_layout = 0;
    ed->readonly_mode = 1;
    ed->cursor = 0;
    ed->view_offset = 0;
    ed->selection_start = (size_t)-1;
    ed->selection_end = (size_t)-1;
    ed->selection_origin = SELECTION_NONE;

    strncpy(ed->filename, "untitled.bin", MAX_PATH_LEN - 1);
    ed->filename[MAX_PATH_LEN - 1] = '\0';

    init_tracker(&ed->tracker, DEFAULT_TRACKER_CAP);
}

void cleanup_editor(HexEditor *ed)
{
    if (ed->fp) {
        fclose(ed->fp);
        ed->fp = NULL;
    }

    free(ed->tracker.items);
    ed->tracker.items = NULL;
    ed->tracker.count = 0;
    ed->tracker.capacity = 0;

    free(ed->mem_buffer);
    ed->mem_buffer = NULL;
}

/* ================================================================== */
/* Selection Helpers                                                   */
/* ================================================================== */

void clear_selection(HexEditor *ed)
{
    ed->selection_start = (size_t)-1;
    ed->selection_end = (size_t)-1;
    ed->selection_origin = SELECTION_NONE;
}

int has_selection(HexEditor *ed)
{
    return ed->selection_start != (size_t)-1 &&
           ed->selection_end != (size_t)-1;
}

size_t sel_min(HexEditor *ed)
{
    return ed->selection_start < ed->selection_end
        ? ed->selection_start : ed->selection_end;
}

size_t sel_max(HexEditor *ed)
{
    return ed->selection_start > ed->selection_end
        ? ed->selection_start : ed->selection_end;
}
