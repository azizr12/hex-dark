#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#include "hex.h"

/* ================================================================== */
/*  Control IDs / Layout Constants                                     */
/* ================================================================== */

#define IDC_VSCROLLBAR  1001

#define MENU_HEIGHT     35
#define STATUS_HEIGHT   28
#define BTN_W           64
#define BTN_H           20
#define BORDER_PX       5

/* ================================================================== */
/*  Configuration                                                      */
/* ================================================================== */

typedef struct {
    int      rows;
    int      aspect_w, aspect_h;
    char     font_name[64];
    int      font_size;

    COLORREF col_background;
    COLORREF col_menu_bg;
    COLORREF col_menu_text;
    COLORREF col_menu_hidden;
    COLORREF col_offset;
    COLORREF col_hex;
    COLORREF col_ascii;
    COLORREF col_selection;
    COLORREF col_cursor;
    COLORREF col_status_bg;
    COLORREF col_status_text;
    COLORREF col_ro_btn;
    COLORREF col_rw_btn;
    
    COLORREF col_highlight_hex;
    COLORREF col_highlight_ascii;
    COLORREF col_scrollbar;

    int      menu_hide_delay;
    int      bytes_per_row;
    int      view_layout;
    int      edit_mode;
    int      tracker_cap;
} AppConfig;

static AppConfig cfg;
static char      ini_path[MAX_PATH];

static HWND      g_hScroll       = NULL;
static RECT      fullClientRect  = {0};

/* ================================================================== */
/*  Undo / Redo History                                                */
/* ================================================================== */

typedef struct {
    size_t  offset;
    uint8_t old_byte;
    uint8_t new_byte;
} ChangeRecord;

#define MAX_UNDO 100000
static ChangeRecord history[MAX_UNDO];
static int history_pos = 0;
static int history_len = 0;

static void record_change(size_t off, uint8_t old_val, uint8_t new_val) {
    if (old_val == new_val) return;
    if (history_pos < MAX_UNDO) {
        history[history_pos].offset = off;
        history[history_pos].old_byte = old_val;
        history[history_pos].new_byte = new_val;
        history_pos++;
        history_len = history_pos; // Discard redo history on new change
    }
}

static void editor_set_byte(size_t off, uint8_t val) {
    uint8_t old_val = get_byte(&editor, off);
    if (old_val != val) {
        record_change(off, old_val, val);
        set_byte(&editor, off, val);
    }
}

static void do_undo(void) {
    if (history_pos > 0) {
        history_pos--;
        ChangeRecord *rec = &history[history_pos];
        set_byte(&editor, rec->offset, rec->old_byte);
    }
}

static void do_redo(void) {
    if (history_pos < history_len) {
        ChangeRecord *rec = &history[history_pos];
        set_byte(&editor, rec->offset, rec->new_byte);
        history_pos++;
    }
}

/* ================================================================== */
/*  INI helpers                                                        */
/* ================================================================== */

static COLORREF ini_get_color(const char *sec, const char *key, COLORREF def)
{
    char buf[64] = {0};
    GetPrivateProfileStringA(sec, key, "", buf, sizeof(buf), ini_path);

    unsigned int r, g, b;
    if (buf[0] == '#') {
        if (sscanf(buf + 1, "%02x%02x%02x", &r, &g, &b) == 3)
            return RGB(r, g, b);
    } else {
        if (sscanf(buf, "%02x%02x%02x", &r, &g, &b) == 3)
            return RGB(r, g, b);
        
        // Fallback for legacy R,G,B format
        int ri, gi, bi;
        if (sscanf(buf, "%d,%d,%d", &ri, &gi, &bi) == 3)
            return RGB(ri, gi, bi);
    }

    return def;
}

static void ini_put_color(const char *sec, const char *key, COLORREF c)
{
    char buf[64];
    sprintf(buf, "#%02X%02X%02X", GetRValue(c), GetGValue(c), GetBValue(c));
    WritePrivateProfileStringA(sec, key, buf, ini_path);
}

static void generate_default_ini(void)
{
    WritePrivateProfileStringA("Window", "rows",       "16",  ini_path);
    WritePrivateProfileStringA("Window", "aspect_w",   "16",  ini_path);
    WritePrivateProfileStringA("Window", "aspect_h",   "9",   ini_path);

    WritePrivateProfileStringA("Font",   "name",       "Consolas", ini_path);
    WritePrivateProfileStringA("Font",   "size",       "16",  ini_path);

    ini_put_color("Colors", "background",      RGB(30,30,30));
    ini_put_color("Colors", "menu_bg",         RGB(45,45,45));
    ini_put_color("Colors", "menu_text",       RGB(220,220,220));
    ini_put_color("Colors", "menu_hidden",     RGB(100,100,100));
    ini_put_color("Colors", "offset",          RGB(0,255,255));
    ini_put_color("Colors", "hex",             RGB(0,255,0));
    ini_put_color("Colors", "ascii",           RGB(255,255,0));
    ini_put_color("Colors", "selection",       RGB(0,50,150));
    ini_put_color("Colors", "cursor",          RGB(0,150,255));
    ini_put_color("Colors", "status_bg",       RGB(40,40,40));
    ini_put_color("Colors", "status_text",     RGB(180,180,180));
    ini_put_color("Colors", "readonly_btn",    RGB(0,180,0));
    ini_put_color("Colors", "overwrite_btn",   RGB(200,0,0));
    
    ini_put_color("Colors", "highlight_hex",   RGB(0,60,120));
    ini_put_color("Colors", "highlight_ascii", RGB(0,60,120));
    ini_put_color("Colors", "scrollbar",       RGB(60,60,60));

    WritePrivateProfileStringA("Behavior", "menu_hide_delay", "2000", ini_path);
    WritePrivateProfileStringA("Behavior", "bytes_per_row",   "16",   ini_path);
    WritePrivateProfileStringA("Behavior", "view_layout",     "0",    ini_path);
    WritePrivateProfileStringA("Behavior", "edit_mode",       "0",    ini_path);

    WritePrivateProfileStringA("Engine", "tracker_initial_capacity",
                               "65536", ini_path);
}

static void load_config(void)
{
    char exe_dir[MAX_PATH];
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);

    char *slash = strrchr(exe_dir, '\\');
    if (slash)
        *(slash + 1) = '\0';

    snprintf(ini_path, MAX_PATH, "%sconfig.ini", exe_dir);

    if (GetFileAttributesA(ini_path) == INVALID_FILE_ATTRIBUTES)
        generate_default_ini();

    cfg.rows       = GetPrivateProfileIntA("Window", "rows", 16, ini_path);
    cfg.aspect_w   = GetPrivateProfileIntA("Window", "aspect_w", 16, ini_path);
    cfg.aspect_h   = GetPrivateProfileIntA("Window", "aspect_h", 9, ini_path);

    GetPrivateProfileStringA("Font", "name", "Consolas",
                             cfg.font_name, sizeof(cfg.font_name), ini_path);

    cfg.font_size  = GetPrivateProfileIntA("Font", "size", 16, ini_path);

    cfg.col_background  = ini_get_color("Colors", "background",      RGB(30,30,30));
    cfg.col_menu_bg     = ini_get_color("Colors", "menu_bg",         RGB(45,45,45));
    cfg.col_menu_text   = ini_get_color("Colors", "menu_text",       RGB(220,220,220));
    cfg.col_menu_hidden = ini_get_color("Colors", "menu_hidden",     RGB(100,100,100));
    cfg.col_offset      = ini_get_color("Colors", "offset",          RGB(0,255,255));
    cfg.col_hex         = ini_get_color("Colors", "hex",             RGB(0,255,0));
    cfg.col_ascii       = ini_get_color("Colors", "ascii",           RGB(255,255,0));
    cfg.col_selection   = ini_get_color("Colors", "selection",       RGB(0,50,150));
    cfg.col_cursor      = ini_get_color("Colors", "cursor",          RGB(0,150,255));
    cfg.col_status_bg   = ini_get_color("Colors", "status_bg",       RGB(40,40,40));
    cfg.col_status_text = ini_get_color("Colors", "status_text",     RGB(180,180,180));
    cfg.col_ro_btn      = ini_get_color("Colors", "readonly_btn",    RGB(0,180,0));
    cfg.col_rw_btn      = ini_get_color("Colors", "overwrite_btn",   RGB(200,0,0));
    
    cfg.col_highlight_hex   = ini_get_color("Colors", "highlight_hex",   RGB(0,60,120));
    cfg.col_highlight_ascii = ini_get_color("Colors", "highlight_ascii", RGB(0,60,120));
    cfg.col_scrollbar       = ini_get_color("Colors", "scrollbar",       RGB(60,60,60));

    cfg.menu_hide_delay = GetPrivateProfileIntA("Behavior", "menu_hide_delay",
                                                2000, ini_path);
    cfg.bytes_per_row   = GetPrivateProfileIntA("Behavior", "bytes_per_row",
                                                16, ini_path);
    cfg.view_layout     = GetPrivateProfileIntA("Behavior", "view_layout",
                                                0, ini_path);
    cfg.edit_mode       = GetPrivateProfileIntA("Behavior", "edit_mode",
                                                0, ini_path);

    cfg.tracker_cap     = GetPrivateProfileIntA("Engine",
                                                "tracker_initial_capacity",
                                                65536, ini_path);

    if (cfg.font_name[0] == '\0')
        strncpy(cfg.font_name, "Consolas", sizeof(cfg.font_name) - 1);

    if (cfg.rows < 1) cfg.rows = 16;
    if (cfg.aspect_w < 1) cfg.aspect_w = 16;
    if (cfg.aspect_h < 1) cfg.aspect_h = 9;
    if (cfg.font_size < 1) cfg.font_size = 16;
    if (cfg.menu_hide_delay < 1) cfg.menu_hide_delay = 2000;
    if (cfg.bytes_per_row < 1) cfg.bytes_per_row = 16;
    if (cfg.view_layout != 1) cfg.view_layout = 0;
    if (cfg.edit_mode != 1) cfg.edit_mode = 0;
    if (cfg.tracker_cap < 64) cfg.tracker_cap = 65536;
}

/* ================================================================== */
/*  Global editor / GUI state                                          */
/* ================================================================== */

static HexEditor editor;

static int       hex_state     = 0;
static uint8_t   temp_hex      = 0;
static HFONT     hFont         = NULL;

static int       charWidth     = 8;
static int       charHeight    = 19;
static int       visibleRows   = 16;

static RECT      clientRect    = {0};
static BOOL      menu_visible  = TRUE;
static int       is_dragging   = 0;

/* ================================================================== */
/*  Window snapping                                                    */
/* ================================================================== */

static void SnapWindowSize(HWND hwnd, int rows)
{
    if (charHeight == 0) return;
    if (rows < 1) rows = 1;

    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);

    int frame_h = (wr.bottom - wr.top) - (cr.bottom - cr.top);
    int frame_w = (wr.right  - wr.left) - (cr.right  - cr.left);

    int client_h = MENU_HEIGHT + STATUS_HEIGHT + rows * charHeight;
    int win_h    = client_h + frame_h;

    int win_w = (int)((double)win_h * (double)cfg.aspect_w / (double)cfg.aspect_h);

    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    int content_chars;
    if (editor.view_layout == 0) {
        content_chars = 10 + bpr * 3 + 2 + bpr + 2;
    } else {
        content_chars = 10 + bpr + 2;
    }

    int min_w = content_chars * charWidth + frame_w + 20;
    if (win_w < min_w) win_w = min_w;

    SetWindowPos(hwnd, NULL, wr.left, wr.top, win_w, win_h, SWP_NOZORDER | SWP_NOMOVE);
}

/* ================================================================== */
/*  Virtual size / row helpers                                         */
/* ================================================================== */

static size_t get_virtual_size(void)
{
    size_t s = get_effective_size(&editor);
    if (!editor.readonly_mode) {
        if (editor.cursor > s) s = editor.cursor;
        if (s < SIZE_MAX) s += 1;
    }
    return s;
}

static size_t get_total_rows(void)
{
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    size_t vs = get_virtual_size();
    size_t rows;
    if (vs > SIZE_MAX - (size_t)bpr)
        rows = SIZE_MAX / (size_t)bpr;
    else
        rows = (vs + (size_t)bpr - 1) / (size_t)bpr;
    if (rows == 0) rows = 1;
    return rows;
}

/* ================================================================== */
/*  Vertical scrollbar                                                 */
/* ================================================================== */

static int VScrollWidth(void) { return GetSystemMetrics(SM_CXVSCROLL); }

static void LayoutScrollBar(HWND hwnd)
{
    (void)hwnd;
    if (!g_hScroll) return;

    int w = VScrollWidth();
    int x = fullClientRect.right - w;
    int y = MENU_HEIGHT;
    int h = fullClientRect.bottom - MENU_HEIGHT - STATUS_HEIGHT;
    if (x < 0) x = 0;
    if (h < 0) h = 0;
    MoveWindow(g_hScroll, x, y, w, h, TRUE);
}

static void UpdateVScroll(HWND hwnd)
{
    (void)hwnd;
    if (!g_hScroll) return;

    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    int pageRows = visibleRows > 0 ? visibleRows : 1;
    size_t totalRows = get_total_rows();

    size_t maxRowIndex = totalRows - 1;
    if (maxRowIndex > (size_t)(INT_MAX - 1)) maxRowIndex = (size_t)(INT_MAX - 1);

    size_t maxScrollRow = (totalRows > (size_t)pageRows) ? (totalRows - (size_t)pageRows) : 0;
    if (maxScrollRow > maxRowIndex) maxScrollRow = maxRowIndex;

    size_t viewRow = editor.view_offset / (size_t)bpr;
    if (viewRow > maxScrollRow) {
        viewRow = maxScrollRow;
        editor.view_offset = viewRow * (size_t)bpr;
    }

    SCROLLINFO si = {0};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = (int)maxRowIndex;
    si.nPage  = (UINT)pageRows;
    si.nPos   = (int)viewRow;

    UINT maxPage = (UINT)(si.nMax + 1);
    if (si.nPage > maxPage) si.nPage = maxPage;
    if (si.nPage < 1) si.nPage = 1;

    SetScrollInfo(g_hScroll, SB_CTL, &si, TRUE);
    if (si.nMax == 0 || (int)si.nPage > si.nMax)
        EnableScrollBar(g_hScroll, SB_CTL, ESB_DISABLE_BOTH);
    else
        EnableScrollBar(g_hScroll, SB_CTL, ESB_ENABLE_BOTH);
}

static void SetViewRow(HWND hwnd, size_t row)
{
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    int pageRows = visibleRows > 0 ? visibleRows : 1;
    size_t totalRows = get_total_rows();
    size_t maxScrollRow = (totalRows > (size_t)pageRows) ? (totalRows - (size_t)pageRows) : 0;
    if (maxScrollRow > (size_t)(INT_MAX - 1)) maxScrollRow = (size_t)(INT_MAX - 1);
    if (row > maxScrollRow) row = maxScrollRow;

    editor.view_offset = row * (size_t)bpr;
    UpdateVScroll(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void HandleVScroll(HWND hwnd, WPARAM wParam, LPARAM lParam)
{
    HWND hsb = (HWND)lParam;
    if (!hsb) hsb = g_hScroll;
    if (!hsb) return;

    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    size_t currentRow = editor.view_offset / (size_t)bpr;
    if (currentRow > (size_t)(INT_MAX - 1)) currentRow = (size_t)(INT_MAX - 1);

    int oldPos = (int)currentRow;
    int pos = oldPos;

    SCROLLINFO si = {0};
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE;
    GetScrollInfo(hsb, SB_CTL, &si);

    int page = si.nPage ? (int)si.nPage : (visibleRows > 0 ? visibleRows : 1);
    int bottom = si.nMax - page + 1;
    if (bottom < 0) bottom = 0;

    switch (LOWORD(wParam)) {
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = bottom; break;
        case SB_LINEUP: pos = oldPos - 1; break;
        case SB_LINEDOWN: pos = oldPos + 1; break;
        case SB_PAGEUP: pos = oldPos - page; break;
        case SB_PAGEDOWN: pos = oldPos + page; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO ti = {0};
            ti.cbSize = sizeof(ti);
            ti.fMask  = SIF_TRACKPOS;
            if (GetScrollInfo(hsb, SB_CTL, &ti)) pos = ti.nTrackPos;
            break;
        }
        case SB_ENDSCROLL:
        default:
            SetFocus(hwnd);
            return;
    }

    if (pos < 0) pos = 0;
    if (pos > bottom) pos = bottom;
    if (pos != oldPos) SetViewRow(hwnd, (size_t)pos);
    SetFocus(hwnd);
}

static void WheelScroll(HWND hwnd, int delta)
{
    if (delta == 0) return;
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    int rows = abs(delta) / WHEEL_DELTA;
    if (rows < 1) rows = 1;
    size_t amount = (size_t)rows * (size_t)bpr;

    if (delta > 0) {
        if (editor.view_offset >= amount) editor.view_offset -= amount;
        else editor.view_offset = 0;
    } else {
        editor.view_offset += amount;
    }
    UpdateVScroll(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void OnSize(HWND hwnd, int cx, int cy)
{
    fullClientRect.left   = 0;
    fullClientRect.top    = 0;
    fullClientRect.right  = cx;
    fullClientRect.bottom = cy;
    clientRect = fullClientRect;

    if (g_hScroll) {
        clientRect.right -= VScrollWidth();
        if (clientRect.right < 0) clientRect.right = 0;
    }

    if (charHeight > 0) {
        visibleRows = (clientRect.bottom - MENU_HEIGHT - STATUS_HEIGHT) / charHeight;
        if (visibleRows < 1) visibleRows = 1;
    }
    LayoutScrollBar(hwnd);
    UpdateVScroll(hwnd);
}

static void CreateVScrollBar(HWND hwnd)
{
    if (g_hScroll) return;
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrA(hwnd, GWLP_HINSTANCE);
    g_hScroll = CreateWindowExA(0, "SCROLLBAR", NULL, WS_CHILD | WS_VISIBLE | SBS_VERT,
                                0, 0, 0, 0, hwnd, (HMENU)(intptr_t)IDC_VSCROLLBAR, hInst, NULL);
    if (g_hScroll) {
        SetWindowTheme(g_hScroll, L"DarkMode_Explorer", NULL);
        LayoutScrollBar(hwnd);
        UpdateVScroll(hwnd);
    }
}

/* ================================================================== */
/*  BPR limiting / cycling                                             */
/* ================================================================== */

static void ClampBPRForLayout(HWND hwnd)
{
    int limit = (editor.view_layout == 1) ? (48 * 4) : 48;
    if (editor.bytes_per_row > limit) {
        editor.bytes_per_row = limit;
        SnapWindowSize(hwnd, visibleRows);
    }
    UpdateVScroll(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void CycleBPR(HWND hwnd)
{
    static const int values[] = { 8, 16, 24, 32, 48, 64, 96, 128, 192 };
    int limit = (editor.view_layout == 1) ? (48 * 4) : 48;
    int current = editor.bytes_per_row;
    int next = 8;

    if (current < limit) {
        next = limit;
        for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
            if (values[i] > current && values[i] <= limit) {
                next = values[i];
                break;
            }
        }
    }
    editor.bytes_per_row = next;
    SnapWindowSize(hwnd, visibleRows);
    UpdateVScroll(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

/* ================================================================== */
/*  Cursor visibility                                                  */
/* ================================================================== */

static void EnsureCursorVisible(void)
{
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    size_t cursor_row = editor.cursor / (size_t)bpr;
    size_t view_row   = editor.view_offset / (size_t)bpr;

    if (cursor_row < view_row) {
        editor.view_offset = cursor_row * (size_t)bpr;
    } else if (cursor_row >= view_row + (size_t)visibleRows) {
        editor.view_offset = (cursor_row - (size_t)visibleRows + 1) * (size_t)bpr;
    }
}

/* ================================================================== */
/*  Clipboard                                                          */
/* ================================================================== */

static void CopySelectionToClipboard(HWND hwnd)
{
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
            if (off >= lo && off <= hi && off < esize)
                p += sprintf(p, "%02X", get_byte(&editor, off));
            else
                p += sprintf(p, "  ");
        }
        *p++ = ' ';
        for (int j = 0; j < bpr; j++) {
            size_t off = row_off + (size_t)j;
            if (off >= lo && off <= hi && off < esize) {
                uint8_t c = get_byte(&editor, off);
                *p++ = isprint(c) ? (char)c : '.';
            } else {
                *p++ = ' ';
            }
        }
        *p++ = '\r';
        *p++ = '\n';
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

static void PasteFromClipboard(HWND hwnd)
{
    if (editor.readonly_mode) return;
    if (!IsClipboardFormatAvailable(CF_TEXT)) return;
    if (!OpenClipboard(hwnd)) return;

    HGLOBAL hMem = GetClipboardData(CF_TEXT);
    if (hMem) {
        char *data = (char *)GlobalLock(hMem);
        if (data) {
            size_t len = strlen(data);
            for (size_t i = 0; i < len; i++) {
                char c = data[i];
                if (c == '\r' || c == '\n') continue;
                
                uint8_t b = (uint8_t)c;
                editor_set_byte(editor.cursor, b);
                editor.cursor++;
            }
            GlobalUnlock(hMem);
        }
    }
    CloseClipboard();
    
    EnsureCursorVisible();
    UpdateVScroll(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

/* ================================================================== */
/*  File save / open helpers                                           */
/* ================================================================== */

static void DoSave(HWND hwnd)
{
    if (editor.memory_mode) {
        OPENFILENAMEA ofn = {0};
        char fn[MAX_PATH] = {0};

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = hwnd;
        ofn.lpstrFile   = fn;
        ofn.nMaxFile    = sizeof(fn);
        ofn.lpstrFilter = "All Files\0*.*\0";
        ofn.Flags       = OFN_OVERWRITEPROMPT;

        if (GetSaveFileNameA(&ofn)) {
            FILE *fp = fopen(fn, "wb");
            if (fp) {
                if (editor.mem_size > 0)
                    fwrite(editor.mem_buffer, 1, editor.mem_size, fp);
                fclose(fp);

                int bpr_keep    = editor.bytes_per_row;
                int layout_keep = editor.view_layout;
                int mode_keep   = editor.edit_mode;
                int ro_keep     = editor.readonly_mode;
                size_t cursor_keep = editor.cursor;
                size_t view_keep   = editor.view_offset;

                cleanup_editor(&editor);
                if (init_file(&editor, fn) != 0)
                    init_memory_mode(&editor);

                editor.bytes_per_row = bpr_keep;
                editor.view_layout   = layout_keep;
                editor.edit_mode     = mode_keep;
                editor.readonly_mode = ro_keep;
                editor.cursor      = cursor_keep;
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

static void DoOpen(HWND hwnd)
{
    OPENFILENAMEA ofn = {0};
    char fn[MAX_PATH] = {0};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFile   = fn;
    ofn.nMaxFile    = sizeof(fn);
    ofn.lpstrFilter = "All Files\0*.*\0";
    ofn.Flags       = OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        cleanup_editor(&editor);
        if (init_file(&editor, fn) != 0)
            init_memory_mode(&editor);

        editor.bytes_per_row = cfg.bytes_per_row;
        editor.view_layout   = cfg.view_layout;
        editor.edit_mode     = cfg.edit_mode;
        editor.readonly_mode = 0;
        editor.cursor      = 0;
        editor.view_offset = 0;
        clear_selection(&editor);
        hex_state = 0;
        
        history_pos = 0;
        history_len = 0;

        ClampBPRForLayout(hwnd);
    }
}

/* ================================================================== */
/*  UI hit-testing                                                     */
/* ================================================================== */

static RECT GetBtnRect(void)
{
    RECT r;
    r.right  = clientRect.right - 8;
    r.left   = r.right - BTN_W;
    r.bottom = clientRect.bottom - 4;
    r.top    = r.bottom - BTN_H;
    if (r.left < 0) { r.left = 0; r.right = BTN_W; }
    if (r.top < 0) { r.top = 0; r.bottom = BTN_H; }
    return r;
}

static size_t GridHitTest(int xPos, int yPos, int *out_mode)
{
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    int xHex = 10 * charWidth;
    int xAscii = (editor.view_layout == 0) ? xHex + (bpr * 3 + 2) * charWidth : xHex;

    if (yPos < MENU_HEIGHT || yPos >= clientRect.bottom - STATUS_HEIGHT)
        return (size_t)-1;

    int row = (yPos - MENU_HEIGHT) / charHeight;
    size_t row_off = editor.view_offset + (size_t)row * (size_t)bpr;
    int col = -1;

    if (editor.view_layout == 0 && xPos >= xHex && xPos < xAscii - 2 * charWidth) {
        col = (xPos - xHex) / (3 * charWidth);
        if (out_mode) *out_mode = 0;
    } else if (xPos >= xAscii) {
        col = (xPos - xAscii) / charWidth;
        if (out_mode) *out_mode = 1;
    }

    if (col >= 0 && col < bpr) {
        size_t off = row_off + (size_t)col;
        size_t vs  = get_virtual_size();
        if (off < vs) return off;
    }
    return (size_t)-1;
}

/* ================================================================== */
/*  Window procedure                                                   */
/* ================================================================== */

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;

    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_NCCALCSIZE: if (wParam) return 0; break;

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        RECT rc; GetClientRect(hwnd, &rc);
        int b = BORDER_PX;

        if (pt.y < b) {
            if (pt.x < b) return HTTOPLEFT;
            else if (pt.x > rc.right - b) return HTTOPRIGHT;
            else return HTTOP;
        }
        if (pt.y > rc.bottom - b) {
            if (pt.x < b) return HTBOTTOMLEFT;
            else if (pt.x > rc.right - b) return HTBOTTOMRIGHT;
            else return HTBOTTOM;
        }
        if (pt.x < b) return HTLEFT;
        if (pt.x > rc.right - b) return HTRIGHT;
        return HTCLIENT;
    }

    case WM_SIZING: {
        if (charHeight == 0) break;
        RECT *r = (RECT *)lParam;
        RECT wr, cr;
        GetWindowRect(hwnd, &wr);
        GetClientRect(hwnd, &cr);
        int fh = (wr.bottom - wr.top) - (cr.bottom - cr.top);
        int avail = (r->bottom - r->top) - fh - MENU_HEIGHT - STATUS_HEIGHT;
        int rows = avail / charHeight;
        if (rows < 1) rows = 1;
        int snapped = MENU_HEIGHT + STATUS_HEIGHT + rows * charHeight + fh;

        switch (wParam) {
            case WMSZ_TOP: case WMSZ_TOPLEFT: case WMSZ_TOPRIGHT:
                r->top = r->bottom - snapped; return TRUE;
            case WMSZ_BOTTOM: case WMSZ_BOTTOMLEFT: case WMSZ_BOTTOMRIGHT:
                r->bottom = r->top + snapped; return TRUE;
            default: return TRUE;
        }
    }

    case WM_CREATE:
        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, 1, (UINT)cfg.menu_hide_delay, NULL);
        hFont = CreateFontA(cfg.font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, cfg.font_name);
        if (!hFont) hFont = (HFONT)GetStockObject(ANSI_FIXED_FONT);

        {
            HDC hdc = GetDC(hwnd);
            SelectObject(hdc, hFont);
            TEXTMETRIC tm;
            GetTextMetrics(hdc, &tm);
            charWidth  = tm.tmAveCharWidth;
            charHeight = tm.tmHeight + tm.tmExternalLeading;
            ReleaseDC(hwnd, hdc);
        }

        SnapWindowSize(hwnd, cfg.rows);
        CreateVScrollBar(hwnd);
        {
            RECT rcNow;
            if (GetClientRect(hwnd, &rcNow)) OnSize(hwnd, rcNow.right, rcNow.bottom);
        }
        ClampBPRForLayout(hwnd);
        SetFocus(hwnd);
        break;

    case WM_SIZE: OnSize(hwnd, LOWORD(lParam), HIWORD(lParam)); break;
    case WM_VSCROLL: HandleVScroll(hwnd, wParam, lParam); break;

    case WM_MOUSEMOVE: {
        POINTS pts = MAKEPOINTS(lParam);
        if (pts.y < MENU_HEIGHT) {
            if (!menu_visible) {
                menu_visible = TRUE;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            SetTimer(hwnd, 1, (UINT)cfg.menu_hide_delay, NULL);
        }
        if (is_dragging && (wParam & MK_LBUTTON)) {
            int m;
            size_t off = GridHitTest(pts.x, pts.y, &m);
            if (off != (size_t)-1) {
                editor.selection_end = off;
                editor.cursor = off;
                hex_state = 0;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        break;
    }

    case WM_TIMER:
        if (wParam == 1 && menu_visible) {
            menu_visible = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT paintRect = fullClientRect;
        if (paintRect.right <= 0 || paintRect.bottom <= 0) paintRect = clientRect;
        if (paintRect.right <= 0 || paintRect.bottom <= 0) { EndPaint(hwnd, &ps); return 0; }

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, paintRect.right, paintRect.bottom);
        if (!memDC || !memBmp) {
            if (memDC) DeleteDC(memDC);
            if (memBmp) DeleteObject(memBmp);
            EndPaint(hwnd, &ps);
            return 0;
        }
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        HBRUSH bgBr = CreateSolidBrush(cfg.col_background);
        FillRect(memDC, &paintRect, bgBr);
        DeleteObject(bgBr);

        SelectObject(memDC, hFont);
        SetBkMode(memDC, TRANSPARENT);

        RECT topR = {0, 0, paintRect.right, MENU_HEIGHT};
        HBRUSH menuBr = CreateSolidBrush(cfg.col_menu_bg);
        FillRect(memDC, &topR, menuBr);
        DeleteObject(menuBr);

        if (menu_visible) {
            SetTextColor(memDC, cfg.col_menu_text);
            const char *menuTxt = "[U]ndo [R]edo [O]pen [S]ave [M]ode [B]PR [V]iew [C]opy [P]ast [X]xit";
            TextOutA(memDC, 15, 8, menuTxt, (int)strlen(menuTxt));
        } else {
            SetTextColor(memDC, cfg.col_menu_hidden);
            const char *fname = editor.filename;
            const char *p1 = strrchr(fname, '\\');
            const char *p2 = strrchr(fname, '/');
            if (p1 || p2) fname = (p1 > p2) ? p1 + 1 : p2 + 1;
            char title[512];
            snprintf(title, sizeof(title), "RAM-Only Hex Editor [%s]", fname);
            TextOutA(memDC, 15, 8, title, (int)strlen(title));
        }

        int y = MENU_HEIGHT;
        int xHex = 10 * charWidth;
        size_t virtual_size = get_virtual_size();
        int selActive = has_selection(&editor);
        size_t sLo = selActive ? sel_min(&editor) : 0;
        size_t sHi = selActive ? sel_max(&editor) : 0;

        for (int i = 0; i < visibleRows; i++) {
            size_t offset = editor.view_offset + (size_t)i * (size_t)bpr;
            if (offset >= virtual_size) break;

            SetTextColor(memDC, cfg.col_offset);
            char offStr[16];
            sprintf(offStr, "%08llX  ", (unsigned long long)offset);
            TextOutA(memDC, 0, y, offStr, (int)strlen(offStr));

            int xAscii;
            if (editor.view_layout == 0) {
                xAscii = xHex + (bpr * 3 + 2) * charWidth;
                for (int j = 0; j < bpr; j++) {
                    size_t off = offset + (size_t)j;
                    if (off >= virtual_size) break;
                    int isSel = selActive && off >= sLo && off <= sHi;
                    int isCur = (off == editor.cursor);

                    if (isSel) {
                        RECT r = { xHex + j * 3 * charWidth, y, xHex + (j + 1) * 3 * charWidth, y + charHeight };
                        HBRUSH sb = CreateSolidBrush(cfg.col_highlight_hex);
                        FillRect(memDC, &r, sb);
                        DeleteObject(sb);
                    }

                    char hexStr[4];
                    sprintf(hexStr, "%02X ", get_byte(&editor, off));
                    if (isCur) {
                        RECT r = { xHex + j * 3 * charWidth, y, xHex + (j + 1) * 3 * charWidth, y + charHeight };
                        HBRUSH cb = CreateSolidBrush(cfg.col_cursor);
                        FillRect(memDC, &r, cb);
                        DeleteObject(cb);
                        SetTextColor(memDC, RGB(0, 0, 0));
                    } else {
                        SetTextColor(memDC, cfg.col_hex);
                    }
                    TextOutA(memDC, xHex + j * 3 * charWidth, y, hexStr, 3);
                }
            } else {
                xAscii = xHex;
            }

            for (int j = 0; j < bpr; j++) {
                size_t off = offset + (size_t)j;
                if (off >= virtual_size) break;
                int isSel = selActive && off >= sLo && off <= sHi;
                int isCur = (off == editor.cursor);

                if (isSel) {
                    RECT r = { xAscii + j * charWidth, y, xAscii + (j + 1) * charWidth, y + charHeight };
                    HBRUSH sb = CreateSolidBrush(cfg.col_highlight_ascii);
                    FillRect(memDC, &r, sb);
                    DeleteObject(sb);
                }

                uint8_t c = get_byte(&editor, off);
                char ch = isprint(c) ? (char)c : '.';
                if (isCur) {
                    RECT r = { xAscii + j * charWidth, y, xAscii + (j + 1) * charWidth, y + charHeight };
                    HBRUSH cb = CreateSolidBrush(cfg.col_cursor);
                    FillRect(memDC, &r, cb);
                    DeleteObject(cb);
                    SetTextColor(memDC, RGB(0, 0, 0));
                } else {
                    SetTextColor(memDC, cfg.col_ascii);
                }
                TextOutA(memDC, xAscii + j * charWidth, y, &ch, 1);
            }
            y += charHeight;
        }

        RECT stR = { 0, paintRect.bottom - STATUS_HEIGHT, paintRect.right, paintRect.bottom };
        HBRUSH stBr = CreateSolidBrush(cfg.col_status_bg);
        FillRect(memDC, &stR, stBr);
        DeleteObject(stBr);
        SetTextColor(memDC, cfg.col_status_text);

        char statusStr[300];
        sprintf(statusStr, " Size: %llu | Off: %08llX | Mode: %s | Layout: %s | BPR: %d | Dirty: %llu%s",
            (unsigned long long)get_effective_size(&editor), (unsigned long long)editor.cursor,
            editor.edit_mode == 0 ? "HEX" : "TEXT", editor.view_layout == 0 ? "HEX+TXT" : "TXT ONLY",
            editor.bytes_per_row, (unsigned long long)editor.tracker.count, editor.memory_mode ? " | MEM" : "");
        TextOutA(memDC, 10, paintRect.bottom - 20, statusStr, (int)strlen(statusStr));

        RECT btnR = GetBtnRect();
        HBRUSH btnBr = CreateSolidBrush(editor.readonly_mode ? cfg.col_ro_btn : cfg.col_rw_btn);
        FillRect(memDC, &btnR, btnBr);
        DeleteObject(btnBr);
        SetTextColor(memDC, RGB(255, 255, 255));
        const char *btnTxt = editor.readonly_mode ? "RO" : "RW";
        DrawTextA(memDC, btnTxt, -1, &btnR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        BitBlt(hdc, 0, 0, paintRect.right, paintRect.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        break;
    }

    case WM_LBUTTONDOWN: {
        int xPos = LOWORD(lParam);
        int yPos = HIWORD(lParam);
        SetFocus(hwnd);

        RECT btnR = GetBtnRect();
        POINT pt = {xPos, yPos};
        if (PtInRect(&btnR, pt)) {
            editor.readonly_mode = 1 - editor.readonly_mode;
            hex_state = 0;
            if (editor.readonly_mode) {
                size_t s = get_effective_size(&editor);
                if (s == 0) editor.cursor = 0;
                else if (editor.cursor >= s) editor.cursor = s - 1;
                clear_selection(&editor);
                EnsureCursorVisible();
            }
            UpdateVScroll(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (yPos < MENU_HEIGHT) {
            if (menu_visible) {
                int block = xPos / (7 * charWidth);
                switch (block) {
                    case 0: do_undo(); EnsureCursorVisible(); UpdateVScroll(hwnd); InvalidateRect(hwnd, NULL, FALSE); break;
                    case 1: do_redo(); EnsureCursorVisible(); UpdateVScroll(hwnd); InvalidateRect(hwnd, NULL, FALSE); break;
                    case 2: DoOpen(hwnd); break;
                    case 3: DoSave(hwnd); break;
                    case 4: editor.edit_mode = 1 - editor.edit_mode; hex_state = 0; break;
                    case 5: CycleBPR(hwnd); break;
                    case 6: editor.view_layout = 1 - editor.view_layout; ClampBPRForLayout(hwnd); break;
                    case 7: CopySelectionToClipboard(hwnd); break;
                    case 8: PasteFromClipboard(hwnd); break;
                    case 9: DestroyWindow(hwnd); break;
                    default:
                        ReleaseCapture();
                        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                        break;
                }
            } else {
                ReleaseCapture();
                SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        int mode;
        size_t off = GridHitTest(xPos, yPos, &mode);
        if (off != (size_t)-1) {
            editor.edit_mode = mode;
            hex_state = 0;
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                if (editor.selection_start == (size_t)-1) editor.selection_start = editor.cursor;
                editor.selection_end = off;
            } else {
                clear_selection(&editor);
                editor.selection_start = off;
                editor.selection_end   = off;
                is_dragging = 1;
                SetCapture(hwnd);
            }
            editor.cursor = off;
            EnsureCursorVisible();
            UpdateVScroll(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }

    case WM_LBUTTONUP:
        if (is_dragging) {
            is_dragging = 0;
            ReleaseCapture();
            if (editor.selection_start == editor.selection_end) clear_selection(&editor);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;

    case WM_MOUSEWHEEL:
        WheelScroll(hwnd, GET_WHEEL_DELTA_WPARAM(wParam));
        break;

    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        char fp[MAX_PATH];
        DragQueryFileA(hDrop, 0, fp, MAX_PATH);
        cleanup_editor(&editor);
        if (init_file(&editor, fp) != 0) init_memory_mode(&editor);
        editor.bytes_per_row = cfg.bytes_per_row;
        editor.view_layout   = cfg.view_layout;
        editor.edit_mode     = cfg.edit_mode;
        editor.readonly_mode = 0;
        editor.cursor      = 0;
        editor.view_offset = 0;
        clear_selection(&editor);
        hex_state = 0;
        history_pos = 0;
        history_len = 0;
        DragFinish(hDrop);
        ClampBPRForLayout(hwnd);
        break;
    }

    case WM_KEYDOWN: {
        int shift = GetKeyState(VK_SHIFT) & 0x8000;
        int ctrl  = GetKeyState(VK_CONTROL) & 0x8000;

        if (ctrl && wParam == 'Z') {
            do_undo();
            EnsureCursorVisible(); UpdateVScroll(hwnd); InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        if (ctrl && wParam == 'Y') {
            do_redo();
            EnsureCursorVisible(); UpdateVScroll(hwnd); InvalidateRect(hwnd, NULL, FALSE);
            break;
        }
        if (ctrl && wParam == 'C') {
            CopySelectionToClipboard(hwnd);
            break;
        }
        if (ctrl && wParam == 'V') {
            PasteFromClipboard(hwnd);
            break;
        }
        if (ctrl && wParam == 'S') {
            DoSave(hwnd);
            break;
        }

        if (shift && editor.selection_start == (size_t)-1) {
            editor.selection_start = editor.cursor;
            editor.selection_end   = editor.cursor;
        }

        size_t old_cursor = editor.cursor;
        size_t vs = get_virtual_size();
        int can_extend = !editor.readonly_mode;
        size_t page = (size_t)bpr * (size_t)visibleRows;

        switch (wParam) {
            case VK_UP:
                if (editor.cursor >= (size_t)bpr) editor.cursor -= (size_t)bpr;
                break;
            case VK_DOWN:
                if (can_extend && editor.memory_mode) editor.cursor += (size_t)bpr;
                else if (editor.cursor + (size_t)bpr < vs) editor.cursor += (size_t)bpr;
                break;
            case VK_LEFT:
                if (editor.cursor > 0) editor.cursor--;
                break;
            case VK_RIGHT:
                if (can_extend && editor.memory_mode) editor.cursor++;
                else if (vs > 0 && editor.cursor < vs - 1) editor.cursor++;
                break;
            case VK_PRIOR:
                if (editor.cursor >= page) editor.cursor -= page;
                else editor.cursor = 0;
                break;
            case VK_NEXT:
                if (can_extend && editor.memory_mode) editor.cursor += page;
                else if (editor.cursor + page < vs) editor.cursor += page;
                else if (vs > 0) editor.cursor = vs - 1;
                break;
            case VK_HOME: editor.cursor = 0; break;
            case VK_END: if (vs > 0) editor.cursor = vs - 1; break;
            case VK_F2: DoSave(hwnd); break;
            case VK_F4: editor.view_layout = 1 - editor.view_layout; ClampBPRForLayout(hwnd); break;
            default: break;
        }

        if (shift) editor.selection_end = editor.cursor;
        else if (editor.cursor != old_cursor) clear_selection(&editor);

        if (editor.cursor != old_cursor) hex_state = 0;
        EnsureCursorVisible();
        UpdateVScroll(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    }

    case WM_CHAR: {
        if (editor.readonly_mode) break;

        if (editor.edit_mode == 0) {
            int val = -1;
            char c = (char)wParam;
            if (c >= '0' && c <= '9') val = c - '0';
            else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;

            if (val != -1) {
                if (hex_state == 0) {
                    temp_hex  = (uint8_t)(val << 4);
                    hex_state = 1;
                } else {
                    uint8_t byte = temp_hex | (uint8_t)val;
                    editor_set_byte(editor.cursor, byte);
                    hex_state = 0;
                    size_t new_size = get_effective_size(&editor);
                    if (editor.memory_mode) editor.cursor++;
                    else if (editor.cursor < new_size) editor.cursor++;
                    EnsureCursorVisible();
                    UpdateVScroll(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        } else {
            if (isprint((int)wParam)) {
                editor_set_byte(editor.cursor, (uint8_t)wParam);
                size_t new_size = get_effective_size(&editor);
                if (editor.memory_mode) editor.cursor++;
                else if (editor.cursor < new_size) editor.cursor++;
                EnsureCursorVisible();
                UpdateVScroll(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        break;
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        cleanup_editor(&editor);
        if (hFont) DeleteObject(hFont);
        PostQuitMessage(0);
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ================================================================== */
/*  WinMain                                                            */
/* ================================================================== */

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hPrev;
    (void)lpCmd;

    load_config();
    memset(&editor, 0, sizeof(editor));

    if (__argc > 1) {
        if (init_file(&editor, __argv[1]) != 0)
            init_memory_mode(&editor);
    } else {
        init_memory_mode(&editor);
    }

    editor.bytes_per_row = cfg.bytes_per_row;
    editor.view_layout   = cfg.view_layout;
    editor.edit_mode     = cfg.edit_mode;
    editor.readonly_mode = 0;

    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = "HexEditorClass";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(WS_EX_ACCEPTFILES | WS_EX_COMPOSITED, "HexEditorClass", "Hex Editor",
                                WS_POPUP | WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT, 700, 1000,
                                NULL, NULL, hInst, NULL);
    SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}