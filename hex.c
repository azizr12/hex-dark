#include "hex.h"

static int seek64(FILE *fp, size_t offset)
{
    return _fseeki64(fp, (__int64)offset, SEEK_SET);
}

static size_t tell64(FILE *fp)
{
    __int64 pos = _ftelli64(fp);
    return pos < 0 ? 0 : (size_t)pos;
}

void init_tracker(DirtyTracker *t, size_t initial_cap)
{
    if (initial_cap < 64) initial_cap = 64;
    t->capacity = initial_cap;
    t->count = 0;
    t->items = (DirtyByte *)malloc(t->capacity * sizeof(DirtyByte));
}

void load_window(HexEditor *ed, size_t target_offset)
{
    int bpr = ed->bytes_per_row;
    if (bpr < 1) bpr = 16;
    ed->window_start = (target_offset / (size_t)bpr) * (size_t)bpr;
    if (seek64(ed->fp, ed->window_start) != 0) {
        ed->window_len = 0;
        return;
    }
    ed->window_len = fread(ed->window, 1, WINDOW_SIZE, ed->fp);
}

size_t get_effective_size(HexEditor *ed)
{
    return ed->memory_mode ? ed->mem_size : ed->file_size;
}

uint8_t get_byte(HexEditor *ed, size_t offset)
{
    if (ed->memory_mode)
        return (offset < ed->mem_size) ? ed->mem_buffer[offset] : 0;
    if (offset >= ed->file_size) return 0;

    for (size_t i = 0; i < ed->tracker.count; i++)
        if (ed->tracker.items[i].offset == offset)
            return ed->tracker.items[i].modified;

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
    if (ed->readonly_mode) return;

    if (ed->memory_mode) {
        if (offset >= ed->mem_capacity) {
            size_t new_cap = ed->mem_capacity ? ed->mem_capacity * 2 : 4096;
            while (new_cap <= offset) {
                if (new_cap > SIZE_MAX / 2) return;
                new_cap *= 2;
            }
            uint8_t *new_buffer = (uint8_t *)realloc(ed->mem_buffer, new_cap);
            if (!new_buffer) return;
            memset(new_buffer + ed->mem_capacity, 0,
                   new_cap - ed->mem_capacity);
            ed->mem_buffer = new_buffer;
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

    if (offset >= ed->file_size) {
        /* Preserve v1.0.9's immediate-extension behavior for now. */
        if (seek64(ed->fp, offset) != 0) return;
        if (fwrite(&value, 1, 1, ed->fp) != 1) return;
        fflush(ed->fp);
        ed->file_size = offset + 1;

        if (ed->tracker.count == ed->tracker.capacity) {
            ed->tracker.capacity *= 2;
            ed->tracker.items = (DirtyByte *)realloc(
                ed->tracker.items,
                ed->tracker.capacity * sizeof(DirtyByte));
        }
        ed->tracker.items[ed->tracker.count].offset = offset;
        ed->tracker.items[ed->tracker.count].original = 0;
        ed->tracker.items[ed->tracker.count].modified = value;
        ed->tracker.count++;
        return;
    }

    for (size_t i = 0; i < ed->tracker.count; i++) {
        if (ed->tracker.items[i].offset == offset) {
            ed->tracker.items[i].modified = value;
            return;
        }
    }

    if (offset < ed->window_start ||
        offset >= ed->window_start + ed->window_len)
        load_window(ed, offset);

    if (offset < ed->window_start ||
        offset >= ed->window_start + ed->window_len)
        return;

    uint8_t true_orig = ed->window[offset - ed->window_start];

    if (ed->tracker.count == ed->tracker.capacity) {
        ed->tracker.capacity *= 2;
        ed->tracker.items = (DirtyByte *)realloc(
            ed->tracker.items,
            ed->tracker.capacity * sizeof(DirtyByte));
    }
    ed->tracker.items[ed->tracker.count].offset = offset;
    ed->tracker.items[ed->tracker.count].original = true_orig;
    ed->tracker.items[ed->tracker.count].modified = value;
    ed->tracker.count++;
}

int save_dirty(HexEditor *ed)
{
    if (ed->memory_mode) return -1;
    for (size_t i = 0; i < ed->tracker.count; i++) {
        if (seek64(ed->fp, ed->tracker.items[i].offset) != 0)
            return -1;
        if (fwrite(&ed->tracker.items[i].modified, 1, 1, ed->fp) != 1)
            return -1;
    }
    fflush(ed->fp);
    ed->tracker.count = 0;
    return 0;
}

int init_file(HexEditor *ed, const char *filename)
{
    ed->fp = fopen(filename, "r+b");
    if (!ed->fp) {
        ed->fp = fopen(filename, "w+b");
        if (!ed->fp) return -1;
    }

    if (_fseeki64(ed->fp, 0, SEEK_END) != 0) {
        fclose(ed->fp);
        ed->fp = NULL;
        return -1;
    }
    ed->file_size = tell64(ed->fp);

    if (seek64(ed->fp, 0) != 0) {
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
    ed->readonly_mode = 0;
    ed->cursor = 0;
    ed->view_offset = 0;
    ed->selection_start = (size_t)-1;
    ed->selection_end = (size_t)-1;
    ed->selection_origin = SELECTION_NONE;
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

void clear_selection(HexEditor *ed)
{
    ed->selection_start = (size_t)-1;
    ed->selection_end = (size_t)-1;
    ed->selection_origin = SELECTION_NONE;
}

int has_selection(HexEditor *ed)
{
    return (ed->selection_start != (size_t)-1 &&
            ed->selection_end != (size_t)-1);
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