#include "app.h"
#include "ui.h"
#include "fileio.h"

void CopySelectionToClipboard(HWND hwnd) {
    if (!has_selection(&editor)) return;
    size_t esize = get_effective_size(&editor);
    if (esize == 0) return;
    size_t lo = sel_min(&editor), hi = sel_max(&editor);
    if (lo >= esize) return;
    if (hi >= esize) hi = esize - 1;
    if (lo > hi) return;

    size_t n = hi - lo + 1;
    int ascii = editor.selection_mode == 1;
    size_t cap = ascii ? n + 1 : n * 3 + 1;
    char *buf = (char *)malloc(cap);
    if (!buf) return;
    char *p = buf;
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = get_byte(&editor, lo + i);
        if (ascii) *p++ = (char)b;
        else { if (i) *p++ = ' '; sprintf(p, "%02X", b); p += 2; }
    }
    *p = '\0';

    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t len = (size_t)(p - buf) + 1;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
        if (hMem) {
            char *dst = (char *)GlobalLock(hMem);
            if (dst) { memcpy(dst, buf, len); GlobalUnlock(hMem); SetClipboardData(CF_TEXT, hMem); }
            else GlobalFree(hMem);
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
                editor.selection_mode = mode_keep;
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
        editor.selection_mode = editor.edit_mode;
        hex_state = 0;
        ClampBPRForLayout(hwnd);
    }
}
