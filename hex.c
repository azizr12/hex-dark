#include "hex.h"

/* ================================================================== */
/*  Dirty Tracker                                                     */
/* ================================================================== */

static int tracker_reserve(DirtyTracker *t, size_t needed)
{
    if (needed <= t->capacity)
        return 0;

    size_t new_capacity = t->capacity ? t->capacity : 64;

    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2)
            return -1;

        new_capacity *= 2;
    }

    DirtyByte *items = (DirtyByte *)realloc(
        t->items,
        new_capacity * sizeof(*items)
    );

    if (!items)
        return -1;

    t->items = items;
    t->capacity = new_capacity;

    return 0;
}

void init_tracker(DirtyTracker *t, size_t initial_cap)
{
    memset(t, 0, sizeof(*t));

    if (initial_cap < 64)
        initial_cap = 64;

    t->items = (DirtyByte *)malloc(
        initial_cap * sizeof(DirtyByte)
    );

    if (!t->items) {
        t->capacity = 0;
        return;
    }

    t->capacity = initial_cap;
    t->count = 0;
}

static DirtyByte *find_dirty(HexEditor *ed, size_t offset)
{
    for (size_t i = 0; i < ed->tracker.count; ++i) {
        if (ed->tracker.items[i].offset == offset)
            return &ed->tracker.items[i];
    }

    return NULL;
}

static int remove_dirty(HexEditor *ed, size_t index)
{
    if (index >= ed->tracker.count)
        return -1;

    if (index + 1 < ed->tracker.count) {
        memmove(
            &ed->tracker.items[index],
            &ed->tracker.items[index + 1],
            (ed->tracker.count - index - 1) *
                sizeof(ed->tracker.items[0])
        );
    }

    ed->tracker.count--;
    return 0;
}

static int track_byte(
    HexEditor *ed,
    size_t offset,
    uint8_t original,
    uint8_t modified
)
{
    DirtyByte *dirty = find_dirty(ed, offset);

    if (dirty) {
        /*
         * If the user changes a byte back to its original value,
         * there is no longer an edit to keep in the overlay.
         */
        if (modified == dirty->original) {
            size_t index =
                (size_t)(dirty - ed->tracker.items);

            return remove_dirty(ed, index);
        }

        dirty->modified = modified;
        return 0;
    }

    /*
     * Don't create a dirty entry for an unchanged existing byte.
     */
    if (offset < ed->original_file_size &&
        modified == original) {
        return 0;
    }

    if (tracker_reserve(
            &ed->tracker,
            ed->tracker.count + 1) != 0) {
        return -1;
    }

    ed->tracker.items[ed->tracker.count].offset = offset;
    ed->tracker.items[ed->tracker.count].original = original;
    ed->tracker.items[ed->tracker.count].modified = modified;

    ed->tracker.count++;

    return 0;
}

/* ================================================================== */
/*  Sliding Window                                                    */
/* ================================================================== */

void load_window(HexEditor *ed, size_t target_offset)
{
    int bpr = ed->bytes_per_row;

    if (bpr < 1)
        bpr = 16;

    ed->window_start =
        (target_offset / (size_t)bpr) * (size_t)bpr;

    ed->window_len = 0;

    if (!ed->fp)
        return;

    if (_fseeki64(
            ed->fp,
            (__int64)ed->window_start,
            SEEK_SET) != 0) {
        return;
    }

    ed->window_len =
        fread(ed->window, 1, WINDOW_SIZE, ed->fp);
}

/* ================================================================== */
/*  Byte Access                                                       */
/* ================================================================== */

size_t get_effective_size(HexEditor *ed)
{
    if (ed->memory_mode)
        return ed->mem_size;

    return ed->file_size;
}

uint8_t get_byte(HexEditor *ed, size_t offset)
{
    /*
     * Memory-backed document.
     */
    if (ed->memory_mode) {
        if (offset >= ed->mem_size)
            return 0;

        return ed->mem_buffer[offset];
    }

    /*
     * Logical EOF.
     */
    if (offset >= ed->file_size)
        return 0;

    /*
     * Unsaved modification takes priority over disk.
     */
    DirtyByte *dirty = find_dirty(ed, offset);

    if (dirty)
        return dirty->modified;

    /*
     * Bytes between physical EOF and logical EOF are
     * implicitly zero until explicitly edited.
     */
    if (offset >= ed->original_file_size)
        return 0;

    /*
     * Load physical file data.
     */
    if (offset < ed->window_start ||
        offset >= ed->window_start + ed->window_len) {
        load_window(ed, offset);
    }

    if (offset < ed->window_start ||
        offset >= ed->window_start + ed->window_len) {
        return 0;
    }

    return ed->window[offset - ed->window_start];
}

/* ================================================================== */
/*  Byte Modification                                                 */
/* ================================================================== */

void set_byte(HexEditor *ed, size_t offset, uint8_t value)
{
    if (!ed)
        return;

    /*
     * Read-only mode is enforced here as the final safety layer.
     */
    if (ed->readonly_mode)
        return;

    /* -------------------------------------------------------------- */
    /* Memory mode                                                     */
    /* -------------------------------------------------------------- */

    if (ed->memory_mode) {

        if (offset >= ed->mem_capacity) {

            size_t new_capacity =
                ed->mem_capacity ?
                ed->mem_capacity :
                4096;

            while (new_capacity <= offset) {

                if (new_capacity > SIZE_MAX / 2)
                    return;

                new_capacity *= 2;
            }

            uint8_t *new_buffer =
                (uint8_t *)realloc(
                    ed->mem_buffer,
                    new_capacity
                );

            if (!new_buffer)
                return;

            memset(
                new_buffer + ed->mem_capacity,
                0,
                new_capacity - ed->mem_capacity
            );

            ed->mem_buffer = new_buffer;
            ed->mem_capacity = new_capacity;
        }

        if (offset >= ed->mem_size) {

            memset(
                ed->mem_buffer + ed->mem_size,
                0,
                offset - ed->mem_size
            );

            ed->mem_size = offset + 1;
        }

        ed->mem_buffer[offset] = value;

        return;
    }

    /* -------------------------------------------------------------- */
    /* File-backed mode                                                */
    /* -------------------------------------------------------------- */

    /*
     * IMPORTANT:
     *
     * Nothing below writes to ed->fp.
     *
     * The physical file remains untouched until save_dirty().
     */

    uint8_t original = 0;

    if (offset < ed->original_file_size) {
        original = get_byte(ed, offset);

        /*
         * get_byte() can return the dirty value. For a brand-new
         * modification we need the TRUE original from disk.
         */
        DirtyByte *existing = find_dirty(ed, offset);

        if (!existing) {
            if (offset < ed->window_start ||
                offset >= ed->window_start + ed->window_len) {
                load_window(ed, offset);
            }

            if (offset >= ed->window_start &&
                offset < ed->window_start + ed->window_len) {
                original =
                    ed->window[offset - ed->window_start];
            }
        } else {
            original = existing->original;
        }
    }

    /*
     * Extend the logical document only.
     *
     * The physical file does NOT grow here.
     */
    if (offset >= ed->file_size)
        ed->file_size = offset + 1;

    /*
     * Track the change in memory.
     *
     * Bytes between old EOF and this offset are implicitly zero.
     */
    (void)track_byte(
        ed,
        offset,
        original,
        value
    );
}

/* ================================================================== */
/*  Save                                                               */
/* ================================================================== */

int save_dirty(HexEditor *ed)
{
    if (!ed)
        return -1;

    /*
     * Memory mode has its own save mechanism in the GUI.
     */
    if (ed->memory_mode)
        return -1;

    if (!ed->fp)
        return -1;

    /*
     * Nothing changed.
     */
    if (ed->tracker.count == 0 &&
        ed->file_size == ed->original_file_size) {
        return 0;
    }

    /*
     * -------------------------------------------------------------- */
    /* Existing file: write modified bytes                            */
    /* -------------------------------------------------------------- */
    if (ed->file_size <= ed->original_file_size) {

        for (size_t i = 0;
             i < ed->tracker.count;
             ++i) {

            DirtyByte *dirty =
                &ed->tracker.items[i];

            if (dirty->offset >= ed->original_file_size)
                continue;

            if (_fseeki64(
                    ed->fp,
                    (__int64)dirty->offset,
                    SEEK_SET) != 0) {
                return -1;
            }

            if (fwrite(
                    &dirty->modified,
                    1,
                    1,
                    ed->fp) != 1) {
                return -1;
            }
        }
    }

    /*
     * -------------------------------------------------------------- */
    /* Extension: grow physical file                                  */
    /* -------------------------------------------------------------- */
    if (ed->file_size > ed->original_file_size) {

        /*
         * Find physical EOF.
         */
        if (_fseeki64(
                ed->fp,
                0,
                SEEK_END) != 0) {
            return -1;
        }

        __int64 physical_end =
            _ftelli64(ed->fp);

        if (physical_end < 0)
            return -1;

        /*
         * Normally this equals original_file_size.
         */
        if ((uintmax_t)physical_end !=
            (uintmax_t)ed->original_file_size) {

            /*
             * Do not silently overwrite a file that changed
             * underneath us.
             */
            return -1;
        }

        /*
         * Grow the file with zero bytes first.
         */
        size_t remaining =
            ed->file_size -
            ed->original_file_size;

        uint8_t zeros[4096] = {0};

        while (remaining > 0) {

            size_t chunk =
                remaining < sizeof(zeros) ?
                remaining :
                sizeof(zeros);

            if (fwrite(
                    zeros,
                    1,
                    chunk,
                    ed->fp) != chunk) {
                return -1;
            }

            remaining -= chunk;
        }

        /*
         * Now overwrite the newly-created bytes with
         * their actual dirty values.
         */
        for (size_t i = 0;
             i < ed->tracker.count;
             ++i) {

            DirtyByte *dirty =
                &ed->tracker.items[i];

            if (dirty->offset <
                ed->original_file_size) {
                continue;
            }

            if (_fseeki64(
                    ed->fp,
                    (__int64)dirty->offset,
                    SEEK_SET) != 0) {
                return -1;
            }

            if (fwrite(
                    &dirty->modified,
                    1,
                    1,
                    ed->fp) != 1) {
                return -1;
            }
        }
    }

    /*
     * Flush only after every write succeeded.
     */
    if (fflush(ed->fp) != 0)
        return -1;

    /*
     * Save succeeded.
     */
    ed->original_file_size = ed->file_size;
    ed->tracker.count = 0;

    return 0;
}

/* ================================================================== */
/*  Initialization                                                    */
/* ================================================================== */

int init_file(HexEditor *ed, const char *filename)
{
    if (!ed || !filename)
        return -1;

    memset(ed, 0, sizeof(*ed));

    /*
     * Open existing file for read/write.
     */
    ed->fp = fopen(filename, "r+b");

    /*
     * If it doesn't exist, create it.
     */
    if (!ed->fp) {
        ed->fp = fopen(filename, "w+b");

        if (!ed->fp)
            return -1;
    }

    /*
     * Determine physical file size using 64-bit Windows
     * file positioning.
     */
    if (_fseeki64(ed->fp, 0, SEEK_END) != 0) {

        fclose(ed->fp);
        ed->fp = NULL;

        return -1;
    }

    __int64 end = _ftelli64(ed->fp);

    if (end < 0 ||
        (uintmax_t)end > (uintmax_t)SIZE_MAX) {

        fclose(ed->fp);
        ed->fp = NULL;

        return -1;
    }

    ed->file_size =
        (size_t)end;

    ed->original_file_size =
        ed->file_size;

    if (_fseeki64(
            ed->fp,
            0,
            SEEK_SET) != 0) {

        fclose(ed->fp);
        ed->fp = NULL;

        return -1;
    }

    strncpy(
        ed->filename,
        filename,
        MAX_PATH_LEN - 1
    );

    ed->filename[MAX_PATH_LEN - 1] = '\0';

    ed->memory_mode = 0;

    ed->cursor = 0;
    ed->view_offset = 0;

    ed->window_start = 0;
    ed->window_len = 0;

    ed->selection_start = (size_t)-1;
    ed->selection_end = (size_t)-1;
    ed->selection_origin = SELECTION_NONE;

    /*
     * IMPORTANT:
     *
     * Newly-opened files start read-only.
     */
    ed->readonly_mode = 1;

    init_tracker(
        &ed->tracker,
        DEFAULT_TRACKER_CAP
    );

    if (!ed->tracker.items)
        return -1;

    return 0;
}

/* ================================================================== */
/*  Memory Mode                                                       */
/* ================================================================== */

void init_memory_mode(HexEditor *ed)
{
    memset(ed, 0, sizeof(*ed));

    ed->memory_mode = 1;

    ed->mem_capacity = 4096;
    ed->mem_size = 0;

    ed->mem_buffer =
        (uint8_t *)calloc(
            ed->mem_capacity,
            1
        );

    ed->bytes_per_row = 16;
    ed->edit_mode = 0;
    ed->view_layout = 0;

    /*
     * Memory documents also start read-only.
     */
    ed->readonly_mode = 1;

    ed->cursor = 0;
    ed->view_offset = 0;

    ed->selection_start = (size_t)-1;
    ed->selection_end = (size_t)-1;
    ed->selection_origin = SELECTION_NONE;

    strncpy(
        ed->filename,
        "untitled.bin",
        MAX_PATH_LEN - 1
    );

    ed->filename[MAX_PATH_LEN - 1] = '\0';

    init_tracker(
        &ed->tracker,
        DEFAULT_TRACKER_CAP
    );
}

/* ================================================================== */
/*  Cleanup                                                            */
/* ================================================================== */

void cleanup_editor(HexEditor *ed)
{
    if (!ed)
        return;

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

    ed->mem_size = 0;
    ed->mem_capacity = 0;
}

/* ================================================================== */
/*  Selection Helpers                                                  */
/* ================================================================== */

void clear_selection(HexEditor *ed)
{
    if (!ed)
        return;

    ed->selection_start = (size_t)-1;
    ed->selection_end = (size_t)-1;
    ed->selection_origin = SELECTION_NONE;
}

int has_selection(HexEditor *ed)
{
    if (!ed)
        return 0;

    return
        ed->selection_start != (size_t)-1 &&
        ed->selection_end != (size_t)-1;
}

size_t sel_min(HexEditor *ed)
{
    if (!has_selection(ed))
        return (size_t)-1;

    return
        ed->selection_start <
        ed->selection_end
        ? ed->selection_start
        : ed->selection_end;
}

size_t sel_max(HexEditor *ed)
{
    if (!has_selection(ed))
        return (size_t)-1;

    return
        ed->selection_start >
        ed->selection_end
        ? ed->selection_start
        : ed->selection_end;
}