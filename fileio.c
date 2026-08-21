#include "app.h"
#include "ui.h"
#include "fileio.h"

void CopySelectionToClipboard(HWND hwnd) {
    if (!has_selection(&editor)) return;
    size_t esize = get_effective_size(&editor);
    if (esize == 0) return;

    size_t lo = sel_min(&editor);
    size_t hi = sel_max(&editor);
    if (lo >= esize) return;
    if (hi >= esize) hi = esize - 1;
    if (lo > hi) return;

    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    size_t row_first = (lo / (size_t)bpr) * (size_t)bpr;
    size_t row_last  = (hi / (size_t)bpr) * (size_t)bpr;
    size_t total_rows = (row_last - row_first) / (size_t)bpr + 1;

    size_t per_row = 12 + (size_t)bpr * 3 + 4;
    char *buf = (char *)malloc(total_rows * per_row + 1);
    if (!buf) return;

    char *p = buf;
    for (size_t row_off = row_first; row_off <= row_last; row_off += (size_t)bpr) {
        p += sprintf(p, "%08llX ", (unsigned long long)row_off);
        for (int j = 0; j < bpr; j++) {
            size_t off = row_off + (size_t)j;
            if (off >= lo && off <= hi && off < esize) p += sprintf(p, "%02X", get_byte(&editor, off));
            else p += sprintf(p, "  ");
        }
        *p++ = ' ';
        for (int j = 0; j < bpr; j++) {
            size_t off = row_off + (size_t)j;
            if (off >= lo && off <= hi && off < esize) {
                uint8_t c = get_byte(&editor, off);
                *p++ = isprint(c) ? (char)c : '.';
            } else { *p++ = ' '; }
        }
        *p++ = '\r'; *p++ = '\n';
    }
    *p = '\0';

    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t len = (size_t)(p - buf) + 1;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
        if (hMem) {
            char *dst = (char *)GlobalLock(hMem);
            memcpy(dst, buf, len);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        }
        CloseClipboard();
    }
    free(buf);
}

void DoSave(HWND hwnd) {
    if (editor.memory_mode) {
        OPENFILENAMEA ofn = {0};
        char fn[MAX_PATH] = {0};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = fn;
        ofn.nMaxFile = sizeof(fn);
        ofn.lpstrFilter = "All Files\0*.*\0";
        ofn.Flags = OFN_OVERWRITEPROMPT;

        if (GetSaveFileNameA(&ofn)) {
            FILE *fp = fopen(fn, "wb");
            if (fp) {
                if (editor.mem_size > 0) fwrite(editor.mem_buffer, 1, editor.mem_size, fp);
                fclose(fp);

                int bpr_keep = editor.bytes_per_row;
                int layout_keep = editor.view_layout;
                int mode_keep = editor.edit_mode;
                int ro_keep = editor.readonly_mode;
                size_t cursor_keep = editor.cursor;
                size_t view_keep = editor.view_offset;

                cleanup_editor(&editor);
                if (init_file(&editor, fn) != 0) init_memory_mode(&editor);

                editor.bytes_per_row = bpr_keep;
                editor.view_layout = layout_keep;
                editor.edit_mode = mode_keep;
                editor.readonly_mode = ro_keep;
                editor.cursor = cursor_keep;
                editor.view_offset = view_keep;

                size_t fsize = get_effective_size(&editor);
                if (fsize == 0) editor.cursor = 0;
                else if (editor.cursor > fsize) editor.cursor = fsize;
                EnsureCursorVisible();
            }
        }
    } else {
        save_dirty(&editor);
    }
    ClampBPRForLayout(hwnd);
}

void DoOpen(HWND hwnd) {
    OPENFILENAMEA ofn = {0};
    char fn[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = fn;
    ofn.nMaxFile = sizeof(fn);
    ofn.lpstrFilter = "All Files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        cleanup_editor(&editor);
        if (init_file(&editor, fn) != 0) init_memory_mode(&editor);

        editor.bytes_per_row = cfg.bytes_per_row;
        editor.view_layout = cfg.view_layout;
        editor.edit_mode = cfg.edit_mode;
        editor.readonly_mode = 0;
        editor.cursor = 0;
        editor.view_offset = 0;
        clear_selection(&editor);
        hex_state = 0;
        ClampBPRForLayout(hwnd);
    }
}