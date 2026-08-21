#include "app.h"
#include "ui.h"
#include "fileio.h"
#include "features.h"
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <io.h>

#define UNDO_MAX_BYTES (64u * 1024u * 1024u)
#define UNDO_MAX_ITEMS 128

typedef struct {
    uint8_t *before;
    uint8_t *after;
    size_t size_before;
    size_t size_after;
    size_t cursor_before;
    size_t cursor_after;
} EditSnapshot;

static WNDPROC original_proc;
static EditSnapshot undo_stack[UNDO_MAX_ITEMS];
static int undo_count;
static EditSnapshot redo_stack[UNDO_MAX_ITEMS];
static int redo_count;
static uint8_t *pending_before;
static size_t pending_size;
static size_t pending_cursor;
static UINT fmt_ascii;
static UINT fmt_hex;

static size_t current_size(void) { return get_effective_size(&editor); }

static uint8_t *capture(size_t *size_out) {
    size_t n = current_size();
    if (n > UNDO_MAX_BYTES) return NULL;
    uint8_t *p = n ? (uint8_t *)malloc(n) : NULL;
    if (n && !p) return NULL;
    for (size_t i = 0; i < n; ++i) p[i] = get_byte(&editor, i);
    *size_out = n;
    return p;
}

static void free_snapshot(EditSnapshot *s) {
    free(s->before); free(s->after);
    memset(s, 0, sizeof(*s));
}

static void clear_stack(EditSnapshot *stack, int *count) {
    for (int i = 0; i < *count; ++i) free_snapshot(&stack[i]);
    *count = 0;
}

static void push_undo(uint8_t *before, size_t sb, size_t cb,
                     uint8_t *after, size_t sa, size_t ca) {
    if (!before && sb) { free(after); return; }
    if (!after && sa) { free(before); return; }
    if (undo_count == UNDO_MAX_ITEMS) {
        free_snapshot(&undo_stack[0]);
        memmove(&undo_stack[0], &undo_stack[1], sizeof(undo_stack[0]) * (UNDO_MAX_ITEMS - 1));
        undo_count--;
    }
    undo_stack[undo_count++] = (EditSnapshot){before, after, sb, sa, cb, ca};
    clear_stack(redo_stack, &redo_count);
}

static int snapshots_equal(uint8_t *a, size_t as, uint8_t *b, size_t bs) {
    return as == bs && (as == 0 || memcmp(a, b, as) == 0);
}

static void begin_edit_tracking(void) {
    free(pending_before); pending_before = NULL; pending_size = 0;
    pending_before = capture(&pending_size);
    pending_cursor = editor.cursor;
}

static void finish_edit_tracking(void) {
    if (!pending_before && pending_size != 0) return;
    size_t after_size = 0;
    uint8_t *after = capture(&after_size);
    if (!after && after_size != 0) { free(pending_before); pending_before = NULL; return; }
    if (!snapshots_equal(pending_before, pending_size, after, after_size) || pending_cursor != editor.cursor) {
        push_undo(pending_before, pending_size, pending_cursor, after, after_size, editor.cursor);
    } else {
        free(pending_before); free(after);
    }
    pending_before = NULL; pending_size = 0;
}

static void set_all_bytes(const uint8_t *data, size_t size) {
    if (editor.memory_mode) {
        if (size > editor.mem_capacity) {
            size_t cap = editor.mem_capacity ? editor.mem_capacity : 4096;
            while (cap < size) cap *= 2;
            uint8_t *p = (uint8_t *)realloc(editor.mem_buffer, cap);
            if (!p) return;
            editor.mem_buffer = p;
            editor.mem_capacity = cap;
        }
        if (size) memcpy(editor.mem_buffer, data, size);
        editor.mem_size = size;
        return;
    }

    size_t old = editor.file_size;
    for (size_t i = 0; i < size; ++i) set_byte(&editor, i, data[i]);
    if (size < old) {
        fflush(editor.fp);
        _chsize_s(_fileno(editor.fp), size);
        editor.file_size = size;
        if (editor.window_start >= size) editor.window_len = 0;
        else if (editor.window_start + editor.window_len > size) editor.window_len = size - editor.window_start;
    }
}

static void apply_before(EditSnapshot *s) {
    set_all_bytes(s->before, s->size_before);
    editor.cursor = s->cursor_before;
    clear_selection(&editor);
    hex_state = 0;
    EnsureCursorVisible();
}

static void apply_after(EditSnapshot *s) {
    set_all_bytes(s->after, s->size_after);
    editor.cursor = s->cursor_after;
    clear_selection(&editor);
    hex_state = 0;
    EnsureCursorVisible();
}

static void undo_edit(HWND hwnd) {
    if (!undo_count) return;
    EditSnapshot s = undo_stack[--undo_count];
    apply_before(&s);
    if (redo_count == UNDO_MAX_ITEMS) {
        free_snapshot(&redo_stack[0]);
        memmove(redo_stack, redo_stack + 1, sizeof(redo_stack[0]) * (UNDO_MAX_ITEMS - 1));
        redo_count--;
    }
    redo_stack[redo_count++] = s;
    InvalidateRect(hwnd, NULL, FALSE);
    UpdateVScroll(hwnd);
}

static void redo_edit(HWND hwnd) {
    if (!redo_count) return;
    EditSnapshot s = redo_stack[--redo_count];
    apply_after(&s);
    if (undo_count == UNDO_MAX_ITEMS) {
        free_snapshot(&undo_stack[0]);
        memmove(undo_stack, undo_stack + 1, sizeof(undo_stack[0]) * (UNDO_MAX_ITEMS - 1));
        undo_count--;
    }
    undo_stack[undo_count++] = s;
    InvalidateRect(hwnd, NULL, FALSE);
    UpdateVScroll(hwnd);
}

static int point_offset(int x, int y, int *mode) {
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    if (y < 35 || y >= clientRect.bottom - 28 || charHeight <= 0) return -1;
    int xHex = 10 * charWidth;
    int xAscii = (editor.view_layout == 0) ? xHex + (bpr * 3 + 2) * charWidth : xHex;
    int row = (y - 35) / charHeight;
    int col = -1;
    if (editor.view_layout == 0 && x >= xHex && x < xAscii - 2 * charWidth) {
        col = (x - xHex) / (3 * charWidth);
        if (mode) *mode = 0;
    } else if (x >= xAscii) {
        col = (x - xAscii) / charWidth;
        if (mode) *mode = 1;
    }
    if (col < 0 || col >= bpr) return -1;
    size_t off = editor.view_offset + (size_t)row * (size_t)bpr + (size_t)col;
    return off < current_size() ? (int)off : -1;
}

static int copy_selection(HWND hwnd) {
    if (!has_selection(&editor)) return 0;
    size_t size = current_size();
    if (!size) return 0;
    size_t lo = sel_min(&editor), hi = sel_max(&editor);
    if (lo >= size) return 0;
    if (hi >= size) hi = size - 1;
    size_t n = hi - lo + 1;
    int ascii = editor.selection_mode == 1;
    size_t cap = ascii ? n + 1 : n * 3 + 1;
    char *buf = (char *)malloc(cap);
    if (!buf) return 0;
    char *p = buf;
    for (size_t i = 0; i < n; ++i) {
        uint8_t b = get_byte(&editor, lo + i);
        if (ascii) *p++ = (char)b;
        else {
            if (i) *p++ = ' ';
            sprintf(p, "%02X", b);
            p += 2;
        }
    }
    *p = '\0';
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        UINT fmt = ascii ? fmt_ascii : fmt_hex;
        HGLOBAL tagged = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(p - buf) + 1);
        if (tagged) {
            void *dst = GlobalLock(tagged);
            if (dst) { memcpy(dst, buf, (size_t)(p - buf) + 1); GlobalUnlock(tagged); SetClipboardData(fmt, tagged); }
            else GlobalFree(tagged);
        }
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(p - buf) + 1);
        if (h) {
            void *dst = GlobalLock(h);
            if (dst) { memcpy(dst, buf, (size_t)(p - buf) + 1); GlobalUnlock(h); SetClipboardData(CF_TEXT, h); }
            else GlobalFree(h);
        }
        CloseClipboard();
    }
    free(buf);
    return 1;
}

static int parse_clipboard(uint8_t **out, size_t *out_n) {
    *out = NULL; *out_n = 0;
    if (!OpenClipboard(NULL)) return 0;
    UINT fmt = 0;
    if (fmt_ascii && IsClipboardFormatAvailable(fmt_ascii)) fmt = fmt_ascii;
    else if (fmt_hex && IsClipboardFormatAvailable(fmt_hex)) fmt = fmt_hex;
    else fmt = CF_TEXT;
    HANDLE h = GetClipboardData(fmt);
    if (!h) { CloseClipboard(); return 0; }
    const char *s = (const char *)GlobalLock(h);
    if (!s) { CloseClipboard(); return 0; }
    size_t len = strlen(s);
    int hex_like = (fmt == fmt_hex);
    if (fmt == CF_TEXT) {
        hex_like = 1;
        size_t digits = 0;
        for (size_t i = 0; i < len; ++i) {
            unsigned char c = (unsigned char)s[i];
            if (isspace(c)) continue;
            if (!isxdigit(c)) { hex_like = 0; break; }
            digits++;
        }
        if (!digits || (digits & 1)) hex_like = 0;
    }
    if (hex_like) {
        size_t digits = 0;
        for (size_t i = 0; i < len; ++i) if (isxdigit((unsigned char)s[i])) digits++;
        if (digits & 1) { GlobalUnlock(h); CloseClipboard(); return 0; }
        size_t n = digits / 2;
        uint8_t *p = (uint8_t *)malloc(n);
        if (!p) { GlobalUnlock(h); CloseClipboard(); return 0; }
        size_t j = 0; int hi = -1;
        for (size_t i = 0; i < len; ++i) if (isxdigit((unsigned char)s[i])) {
            int v = isdigit((unsigned char)s[i]) ? s[i] - '0' : tolower((unsigned char)s[i]) - 'a' + 10;
            if (hi < 0) hi = v; else { p[j++] = (uint8_t)((hi << 4) | v); hi = -1; }
        }
        *out = p; *out_n = n;
    } else {
        uint8_t *p = (uint8_t *)malloc(len);
        if (!p && len) { GlobalUnlock(h); CloseClipboard(); return 0; }
        memcpy(p, s, len);
        *out = p; *out_n = len;
    }
    GlobalUnlock(h);
    CloseClipboard();
    return 1;
}

static int paste_overwrite(HWND hwnd) {
    if (editor.readonly_mode) return 0;
    uint8_t *data = NULL; size_t n = 0;
    if (!parse_clipboard(&data, &n) || !n) { free(data); return 0; }
    size_t start = editor.cursor;
    size_t old_size = current_size();
    if (start > old_size) start = old_size;
    begin_edit_tracking();
    for (size_t i = 0; i < n; ++i) set_byte(&editor, start + i, data[i]);
    editor.cursor = start + n;
    if (editor.cursor > get_effective_size(&editor)) editor.cursor = get_effective_size(&editor);
    clear_selection(&editor);
    hex_state = 0;
    finish_edit_tracking();
    EnsureCursorVisible();
    UpdateVScroll(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
    free(data);
    return 1;
}

static LRESULT CALLBACK FeatureWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        int ctrl = GetKeyState(VK_CONTROL) & 0x8000;
        if (ctrl && (wParam == 'Z' || wParam == 'z')) { undo_edit(hwnd); return 0; }
        if (ctrl && (wParam == 'Y' || wParam == 'y')) { redo_edit(hwnd); return 0; }
        if (ctrl && (wParam == 'V' || wParam == 'v')) { paste_overwrite(hwnd); return 0; }
        if (ctrl && (wParam == 'C' || wParam == 'c') && has_selection(&editor)) { copy_selection(hwnd); return 0; }
        if (wParam == VK_BACK || wParam == VK_DELETE || wParam == VK_INSERT) begin_edit_tracking();
    } else if (msg == WM_CHAR) {
        begin_edit_tracking();
    } else if (msg == WM_LBUTTONDOWN) {
        int mode = 0;
        int off = point_offset(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &mode);
        if (off >= 0 && has_selection(&editor) && (size_t)off >= sel_min(&editor) && (size_t)off <= sel_max(&editor)) {
            copy_selection(hwnd);
            return 0;
        }
    }

    LRESULT r = CallWindowProcA(original_proc, hwnd, msg, wParam, lParam);
    if (msg == WM_LBUTTONDOWN && has_selection(&editor)) editor.selection_mode = editor.edit_mode;
    if (msg == WM_KEYDOWN || msg == WM_CHAR) finish_edit_tracking();
    return r;
}

void FeaturesInstall(HWND hwnd) {
    fmt_ascii = RegisterClipboardFormatA("HexDark ASCII");
    fmt_hex = RegisterClipboardFormatA("HexDark HEX");
    original_proc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)FeatureWndProc);
}

static void enable_dark_mode_for_window(HWND hwnd) {
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    typedef BOOL (WINAPI *AllowDarkModeForWindowFn)(HWND, BOOL);
    AllowDarkModeForWindowFn allow = (AllowDarkModeForWindowFn)GetProcAddress(ux, MAKEINTRESOURCEA(133));
    if (allow) allow(hwnd, TRUE);
}

void FeaturesApplyDarkScrollbar(HWND hwnd) {
    enable_dark_mode_for_window(hwnd);
    if (!g_hScroll) return;
    enable_dark_mode_for_window(g_hScroll);
    SetWindowTheme(g_hScroll, L"DarkMode_Explorer", NULL);
    InvalidateRect(g_hScroll, NULL, TRUE);
}
