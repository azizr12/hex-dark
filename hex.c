#include "hex.h"

/* ================================================================== */
/*  Dirty Tracker                                                     */
/* ================================================================== */

static int tracker_reserve(DirtyTracker *t, size_t needed) {
    if (needed <= t->capacity) return 0;
    size_t new_capacity = t->capacity ? t->capacity : 64;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2) return -1;
        new_capacity *= 2;
    }
    DirtyByte *items = (DirtyByte *)realloc(t->items, new_capacity * sizeof(*items));
    if (!items) return -1;
    t->items = items;
    t->capacity = new_capacity;
    return 0;
}

void init_tracker(DirtyTracker *t, size_t initial_cap) {
    memset(t, 0, sizeof(*t));
    if (initial_cap < 64) initial_cap = 64;
    t->items = (DirtyByte *)malloc(initial_cap * sizeof(DirtyByte));
    if (!t->items) { t->capacity = 0; return; }
    t->capacity = initial_cap;
    t->count = 0;
}

static DirtyByte *find_dirty(HexEditor *ed, size_t offset) {
    for (size_t i = 0; i < ed->tracker.count; ++i) {
        if (ed->tracker.items[i].offset == offset) return &ed->tracker.items[i];
    }
    return NULL;
}

static int remove_dirty(HexEditor *ed, size_t index) {
    if (index >= ed->tracker.count) return -1;
    if (index + 1 < ed->tracker.count) {
        memmove(&ed->tracker.items[index], &ed->tracker.items[index + 1],
                (ed->tracker.count - index - 1) * sizeof(ed->tracker.items[0]));
    }
    ed->tracker.count--;
    return 0;
}

static int track_byte(HexEditor *ed, size_t offset, uint8_t original, uint8_t modified) {
    DirtyByte *dirty = find_dirty(ed, offset);
    if (dirty) {
        if (modified == dirty->original) {
            size_t index = (size_t)(dirty - ed->tracker.items);
            return remove_dirty(ed, index);
        }
        dirty->modified = modified;
        return 0;
    }
    if (offset < ed->original_file_size && modified == original) return 0;
    if (tracker_reserve(&ed->tracker, ed->tracker.count + 1) != 0) return -1;
    ed->tracker.items[ed->tracker.count].offset = offset;
    ed->tracker.items[ed->tracker.count].original = original;
    ed->tracker.items[ed->tracker.count].modified = modified;
    ed->tracker.count++;
    return 0;
}

/* ================================================================== */
/*  Edit History (Undo/Redo)                                          */
/* ================================================================== */

void undo(HexEditor *ed) {
    if (!ed || ed->history.current == 0) return;
    ed->history.current--;
    HistoryEntry *entry = &ed->history.entries[ed->history.current];
    ed->recording_history = 0;
    for (size_t i = 0; i < entry->length; i++) set_byte(ed, entry->offset + i, entry->old_data[i]);
    ed->recording_history = 1;
}

void redo(HexEditor *ed) {
    if (!ed || ed->history.current >= ed->history.count) return;
    HistoryEntry *entry = &ed->history.entries[ed->history.current];
    ed->recording_history = 0;
    for (size_t i = 0; i < entry->length; i++) set_byte(ed, entry->offset + i, entry->new_data[i]);
    ed->recording_history = 1;
    ed->history.current++;
}

void paste_bytes(HexEditor *ed, size_t offset, const uint8_t *data, size_t length) {
    if (!ed || !data || length == 0 || ed->readonly_mode) return;
    
    uint8_t *old_data = (uint8_t *)malloc(length);
    for (size_t i = 0; i < length; i++) old_data[i] = get_byte(ed, offset + i);
    
    if (ed->history.current < ed->history.count) {
        for (size_t i = ed->history.current; i < ed->history.count; i++) {
            free(ed->history.entries[i].old_data);
            free(ed->history.entries[i].new_data);
        }
        ed->history.count = ed->history.current;
    }
    if (ed->history.count >= ed->history.capacity) {
        size_t new_cap = ed->history.capacity ? ed->history.capacity * 2 : 256;
        ed->history.entries = (HistoryEntry *)realloc(ed->history.entries, new_cap * sizeof(HistoryEntry));
        ed->history.capacity = new_cap;
    }
    
    HistoryEntry *entry = &ed->history.entries[ed->history.current];
    entry->offset = offset;
    entry->length = length;
    entry->old_data = old_data;
    entry->new_data = (uint8_t *)malloc(length);
    memcpy(entry->new_data, data, length);
    ed->history.current++;
    ed->history.count++;

    ed->recording_history = 0;
    for (size_t i = 0; i < length; i++) set_byte(ed, offset + i, data[i]);
    ed->recording_history = 1;
}

/* ================================================================== */
/*  Sliding Window & Byte Access                                      */
/* ================================================================== */

void load_window(HexEditor *ed, size_t target_offset) {
    int bpr = ed->bytes_per_row;
    if (bpr < 1) bpr = 16;
    ed->window_start = (target_offset / (size_t)bpr) * (size_t)bpr;
    ed->window_len = 0;
    if (!ed->fp) return;
    if (_fseeki64(ed->fp, (__int64)ed->window_start, SEEK_SET) != 0) return;
    ed->window_len = fread(ed->window, 1, WINDOW_SIZE, ed->fp);
}

size_t get_effective_size(HexEditor *ed) {
    if (ed->memory_mode) return ed->mem_size;
    return ed->file_size;
}

uint8_t get_byte(HexEditor *ed, size_t offset) {
    if (ed->memory_mode) {
        if (offset >= ed->mem_size) return 0;
        return ed->mem_buffer[offset];
    }
    if (offset >= ed->file_size) return 0;
    DirtyByte *dirty = find_dirty(ed, offset);
    if (dirty) return dirty->modified;
    if (offset >= ed->original_file_size) return 0;
    if (offset < ed->window_start || offset >= ed->window_start + ed->window_len) load_window(ed, offset);
    if (offset < ed->window_start || offset >= ed->window_start + ed->window_len) return 0;
    return ed->window[offset - ed->window_start];
}

/* ================================================================== */
/*  Byte Modification (NO DIRECT FILE WRITE)                          */
/* ================================================================== */

void set_byte(HexEditor *ed, size_t offset, uint8_t value) {
    if (!ed || ed->readonly_mode) return;
    uint8_t old_value = get_byte(ed, offset);
    if (old_value == value) return;

    if (ed->recording_history) {
        if (ed->history.current < ed->history.count) {
            for (size_t i = ed->history.current; i < ed->history.count; i++) {
                free(ed->history.entries[i].old_data);
                free(ed->history.entries[i].new_data);
            }
            ed->history.count = ed->history.current;
        }
        if (ed->history.count >= ed->history.capacity) {
            size_t new_cap = ed->history.capacity ? ed->history.capacity * 2 : 256;
            ed->history.entries = (HistoryEntry *)realloc(ed->history.entries, new_cap * sizeof(HistoryEntry));
            ed->history.capacity = new_cap;
        }
        ed->history.entries[ed->history.current].offset = offset;
        ed->history.entries[ed->history.current].length = 1;
        ed->history.entries[ed->history.current].old_data = (uint8_t *)malloc(1);
        ed->history.entries[ed->history.current].new_data = (uint8_t *)malloc(1);
        *ed->history.entries[ed->history.current].old_data = old_value;
        *ed->history.entries[ed->history.current].new_data = value;
        ed->history.current++;
        ed->history.count++;
    }

    if (ed->memory_mode) {
        if (offset >= ed->mem_capacity) {
            size_t new_capacity = ed->mem_capacity ? ed->mem_capacity : 4096;
            while (new_capacity <= offset) new_capacity *= 2;
            uint8_t *new_buffer = (uint8_t *)realloc(ed->mem_buffer, new_capacity);
            if (!new_buffer) return;
            memset(new_buffer + ed->mem_capacity, 0, new_capacity - ed->mem_capacity);
            ed->mem_buffer = new_buffer;
            ed->mem_capacity = new_capacity;
        }
        if (offset >= ed->mem_size) {
            memset(ed->mem_buffer + ed->mem_size, 0, offset - ed->mem_size);
            ed->mem_size = offset + 1;
        }
        ed->mem_buffer[offset] = value;
        return;
    }

    uint8_t original = 0;
    if (offset < ed->original_file_size) {
        DirtyByte *existing = find_dirty(ed, offset);
        if (existing) {
            original = existing->original;
        } else {
            if (offset < ed->window_start || offset >= ed->window_start + ed->window_len) load_window(ed, offset);
            if (offset >= ed->window_start && offset < ed->window_start + ed->window_len) {
                original = ed->window[offset - ed->window_start];
            }
        }
    }

    if (offset >= ed->file_size) ed->file_size = offset + 1;
    (void)track_byte(ed, offset, original, value);
}

/* ================================================================== */
/*  Save                                                              */
/* ================================================================== */

int save_dirty(HexEditor *ed) {
    if (!ed || ed->memory_mode || !ed->fp) return -1;
    if (ed->tracker.count == 0 && ed->file_size == ed->original_file_size) return 0;

    if (ed->file_size <= ed->original_file_size) {
        for (size_t i = 0; i < ed->tracker.count; ++i) {
            DirtyByte *dirty = &ed->tracker.items[i];
            if (dirty->offset >= ed->original_file_size) continue;
            if (_fseeki64(ed->fp, (__int64)dirty->offset, SEEK_SET) != 0) return -1;
            if (fwrite(&dirty->modified, 1, 1, ed->fp) != 1) return -1;
        }
    }

    if (ed->file_size > ed->original_file_size) {
        if (_fseeki64(ed->fp, 0, SEEK_END) != 0) return -1;
        __int64 physical_end = _ftelli64(ed->fp);
        if (physical_end < 0 || (uintmax_t)physical_end != (uintmax_t)ed->original_file_size) return -1;

        size_t remaining = ed->file_size - ed->original_file_size;
        uint8_t zeros[4096] = {0};
        while (remaining > 0) {
            size_t chunk = remaining < sizeof(zeros) ? remaining : sizeof(zeros);
            if (fwrite(zeros, 1, chunk, ed->fp) != chunk) return -1;
            remaining -= chunk;
        }

        for (size_t i = 0; i < ed->tracker.count; ++i) {
            DirtyByte *dirty = &ed->tracker.items[i];
            if (dirty->offset < ed->original_file_size) continue;
            if (_fseeki64(ed->fp, (__int64)dirty->offset, SEEK_SET) != 0) return -1;
            if (fwrite(&dirty->modified, 1, 1, ed->fp) != 1) return -1;
        }
    }

    if (fflush(ed->fp) != 0) return -1;
    ed->original_file_size = ed->file_size;
    ed->tracker.count = 0;
    return 0;
}

/* ================================================================== */
/*  Initialization & Cleanup                                          */
/* ================================================================== */

int init_file(HexEditor *ed, const char *filename) {
    if (!ed || !filename) return -1;
    memset(ed, 0, sizeof(*ed));

    ed->fp = fopen(filename, "r+b");
    if (!ed->fp) {
        ed->fp = fopen(filename, "rb");
        if (ed->fp) ed->readonly_mode = 1;
        else {
            ed->fp = fopen(filename, "w+b");
            if (!ed->fp) return -1;
        }
    }

    if (_fseeki64(ed->fp, 0, SEEK_END) != 0) { fclose(ed->fp); ed->fp = NULL; return -1; }
    __int64 end = _ftelli64(ed->fp);
    if (end < 0 || (uintmax_t)end > (uintmax_t)SIZE_MAX) { fclose(ed->fp); ed->fp = NULL; return -1; }

    ed->file_size = (size_t)end;
    ed->original_file_size = ed->file_size;
    _fseeki64(ed->fp, 0, SEEK_SET);

    strncpy(ed->filename, filename, MAX_PATH_LEN - 1);
    ed->filename[MAX_PATH_LEN - 1] = '\0';
    ed->memory_mode = 0;
    ed->cursor = 0;
    ed->view_offset = 0;
    ed->selection_start = (size_t)-1;
    ed->selection_end = (size_t)-1;
    ed->selection_origin = SELECTION_NONE;
    
    ed->history.entries = NULL;
    ed->history.count = 0;
    ed->history.capacity = 0;
    ed->history.current = 0;
    ed->recording_history = 1;

    init_tracker(&ed->tracker, DEFAULT_TRACKER_CAP);
    return ed->tracker.items ? 0 : -1;
}

void init_memory_mode(HexEditor *ed) {
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
    
    ed->history.entries = NULL;
    ed->history.count = 0;
    ed->history.capacity = 0;
    ed->history.current = 0;
    ed->recording_history = 1;

    init_tracker(&ed->tracker, DEFAULT_TRACKER_CAP);
}

void cleanup_editor(HexEditor *ed) {
    if (!ed) return;
    if (ed->fp) { fclose(ed->fp); ed->fp = NULL; }
    
    free(ed->tracker.items);
    ed->tracker.items = NULL; ed->tracker.count = 0; ed->tracker.capacity = 0;
    
    free(ed->mem_buffer);
    ed->mem_buffer = NULL; ed->mem_size = 0; ed->mem_capacity = 0;

    for (size_t i = 0; i < ed->history.count; i++) {
        free(ed->history.entries[i].old_data);
        free(ed->history.entries[i].new_data);
    }
    free(ed->history.entries);
    ed->history.entries = NULL;
    ed->history.count = 0;
    ed->history.capacity = 0;
    ed->history.current = 0;
    ed->recording_history = 1;
}

/* ================================================================== */
/*  Selection Helpers                                                 */
/* ================================================================== */

void clear_selection(HexEditor *ed) {
    if (!ed) return;
    ed->selection_start = (size_t)-1;
    ed->selection_end = (size_t)-1;
    ed->selection_origin = SELECTION_NONE;
}

int has_selection(HexEditor *ed) {
    if (!ed) return 0;
    return ed->selection_start != (size_t)-1 && ed->selection_end != (size_t)-1;
}

size_t sel_min(HexEditor *ed) {
    if (!has_selection(ed)) return (size_t)-1;
    return ed->selection_start < ed->selection_end ? ed->selection_start : ed->selection_end;
}

size_t sel_max(HexEditor *ed) {
    if (!has_selection(ed)) return (size_t)-1;
    return ed->selection_start > ed->selection_end ? ed->selection_start : ed->selection_end;
}