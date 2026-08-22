#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <windowsx.h>
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
/* Control IDs / Layout                                                */
/* ================================================================== */

#define IDC_VSCROLLBAR 1001

#define MENU_HEIGHT     35
#define STATUS_HEIGHT   28
#define BTN_W           64
#define BTN_H           24
#define BORDER_PX       5

/* ================================================================== */
/* Configuration                                                       */
/* ================================================================== */

typedef struct {
    int rows;
    int aspect_w;
    int aspect_h;
    int window_width;
    int window_height;

    char font_name[64];
    int font_size;

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

    int menu_hide_delay;
    int bytes_per_row;
    int view_layout;
    int edit_mode;
    int tracker_cap;
} AppConfig;

static AppConfig cfg;
static char ini_path[MAX_PATH + 16];

/* ================================================================== */
/* Global Windows state                                                */
/* ================================================================== */

static RECT fullClientRect = {0};
static RECT clientRect = {0};

static HFONT hFont = NULL;

static int charWidth = 8;
static int charHeight = 19;
static int visibleRows = 16;

static BOOL menu_visible = TRUE;
static int is_dragging = 0;

/* ================================================================== */
/* Global editor state                                                 */
/* ================================================================== */

static HexEditor editor;

static int hex_state = 0;
static uint8_t temp_hex = 0;

/* ================================================================== */
/* Color helpers                                                       */
/* ================================================================== */

static COLORREF ini_get_color(const char *section,
                              const char *key,
                              COLORREF default_color)
{
    char buffer[64] = {0};

    GetPrivateProfileStringA(
        section,
        key,
        "",
        buffer,
        sizeof(buffer),
        ini_path
    );

    if (buffer[0] == '#') {
        unsigned int rgb = 0;

        if (sscanf(buffer + 1, "%06X", &rgb) == 1) {
            int r = (rgb >> 16) & 0xFF;
            int g = (rgb >> 8) & 0xFF;
            int b = rgb & 0xFF;

            return RGB(r, g, b);
        }
    }

    {
        unsigned int rgb = 0;

        if (strlen(buffer) == 6 &&
            sscanf(buffer, "%06X", &rgb) == 1) {

            int r = (rgb >> 16) & 0xFF;
            int g = (rgb >> 8) & 0xFF;
            int b = rgb & 0xFF;

            return RGB(r, g, b);
        }
    }

    {
        int r;
        int g;
        int b;

        if (sscanf(buffer, "%d,%d,%d", &r, &g, &b) == 3)
            return RGB(r, g, b);
    }

    return default_color;
}

static void ini_put_color(const char *section,
                          const char *key,
                          COLORREF color)
{
    char buffer[16];

    snprintf(
        buffer,
        sizeof(buffer),
        "#%02X%02X%02X",
        GetRValue(color),
        GetGValue(color),
        GetBValue(color)
    );

    WritePrivateProfileStringA(
        section,
        key,
        buffer,
        ini_path
    );
}

/* ================================================================== */
/* Default configuration                                               */
/* ================================================================== */

static void generate_default_ini(void)
{
    WritePrivateProfileStringA("Window", "rows", "20", ini_path);
    WritePrivateProfileStringA("Window", "aspect_w", "16", ini_path);
    WritePrivateProfileStringA("Window", "aspect_h", "9", ini_path);
    WritePrivateProfileStringA("Window", "width", "900", ini_path);
    WritePrivateProfileStringA("Window", "height", "900", ini_path);
    WritePrivateProfileStringA("Font", "name", "Consolas", ini_path);
    WritePrivateProfileStringA("Font", "size", "16", ini_path);

    ini_put_color("Colors", "background", RGB(30, 30, 30));
    ini_put_color("Colors", "menu_bg", RGB(45, 45, 45));
    ini_put_color("Colors", "menu_text", RGB(220, 220, 220));
    ini_put_color("Colors", "menu_hidden", RGB(100, 100, 100));
    ini_put_color("Colors", "offset", RGB(0, 255, 255));
    ini_put_color("Colors", "hex", RGB(0, 255, 0));
    ini_put_color("Colors", "ascii", RGB(255, 255, 0));
    ini_put_color("Colors", "selection", RGB(0, 50, 150));
    ini_put_color("Colors", "cursor", RGB(0, 150, 255));
    ini_put_color("Colors", "status_bg", RGB(40, 40, 40));
    ini_put_color("Colors", "status_text", RGB(180, 180, 180));
    ini_put_color("Colors", "readonly_btn", RGB(0, 180, 0));
    ini_put_color("Colors", "overwrite_btn", RGB(200, 0, 0));

    WritePrivateProfileStringA("Behavior", "menu_hide_delay", "2000", ini_path);
    WritePrivateProfileStringA("Behavior", "bytes_per_row", "16", ini_path);
    WritePrivateProfileStringA("Behavior", "view_layout", "0", ini_path);
    WritePrivateProfileStringA("Behavior", "edit_mode", "0", ini_path);
    WritePrivateProfileStringA("Engine", "tracker_initial_capacity", "65536", ini_path);
}

/* ================================================================== */
/* Configuration loading                                               */
/* ================================================================== */

static void load_config(void)
{
    char exe_path[MAX_PATH];

    GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));

    char *slash = strrchr(exe_path, '\\');

    if (slash)
        *(slash + 1) = '\0';

    snprintf(ini_path, sizeof(ini_path), "%sconfig.ini", exe_path);

    if (GetFileAttributesA(ini_path) == INVALID_FILE_ATTRIBUTES)
        generate_default_ini();

    cfg.window_width = GetPrivateProfileIntA("Window", "width", 900, ini_path);
    cfg.window_height = GetPrivateProfileIntA("Window", "height", 900, ini_path);

    cfg.rows = GetPrivateProfileIntA("Window", "rows", 20, ini_path);
    cfg.aspect_w = GetPrivateProfileIntA("Window", "aspect_w", 16, ini_path);
    cfg.aspect_h = GetPrivateProfileIntA("Window", "aspect_h", 9, ini_path);

    GetPrivateProfileStringA("Font", "name", "Consolas", cfg.font_name, sizeof(cfg.font_name), ini_path);
    cfg.font_size = GetPrivateProfileIntA("Font", "size", 16, ini_path);

    cfg.col_background = ini_get_color("Colors", "background", RGB(30, 30, 30));
    cfg.col_menu_bg = ini_get_color("Colors", "menu_bg", RGB(45, 45, 45));
    cfg.col_menu_text = ini_get_color("Colors", "menu_text", RGB(220, 220, 220));
    cfg.col_menu_hidden = ini_get_color("Colors", "menu_hidden", RGB(100, 100, 100));
    cfg.col_offset = ini_get_color("Colors", "offset", RGB(0, 255, 255));
    cfg.col_hex = ini_get_color("Colors", "hex", RGB(0, 255, 0));
    cfg.col_ascii = ini_get_color("Colors", "ascii", RGB(255, 255, 0));
    cfg.col_selection = ini_get_color("Colors", "selection", RGB(0, 50, 150));
    cfg.col_cursor = ini_get_color("Colors", "cursor", RGB(0, 150, 255));
    cfg.col_status_bg = ini_get_color("Colors", "status_bg", RGB(40, 40, 40));
    cfg.col_status_text = ini_get_color("Colors", "status_text", RGB(180, 180, 180));
    cfg.col_ro_btn = ini_get_color("Colors", "readonly_btn", RGB(0, 180, 0));
    cfg.col_rw_btn = ini_get_color("Colors", "overwrite_btn", RGB(200, 0, 0));

    cfg.menu_hide_delay = GetPrivateProfileIntA("Behavior", "menu_hide_delay", 2000, ini_path);
    cfg.bytes_per_row = GetPrivateProfileIntA("Behavior", "bytes_per_row", 16, ini_path);
    cfg.view_layout = GetPrivateProfileIntA("Behavior", "view_layout", 0, ini_path);
    cfg.edit_mode = GetPrivateProfileIntA("Behavior", "edit_mode", 0, ini_path);
    cfg.tracker_cap = GetPrivateProfileIntA("Engine", "tracker_initial_capacity", 65536, ini_path);

    if (cfg.font_name[0] == '\0') {
        strncpy(cfg.font_name, "Consolas", sizeof(cfg.font_name) - 1);
        cfg.font_name[sizeof(cfg.font_name) - 1] = '\0';
    }

    if (cfg.rows < 1) cfg.rows = 20;
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
/* Window sizing                                                       */
/* ================================================================== */

static void SnapWindowSize(HWND hwnd, int rows)
{
    if (charHeight == 0) return;
    if (rows < 1) rows = 1;

    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);

    int frame_h = (wr.bottom - wr.top) - (cr.bottom - cr.top);
    int frame_w = (wr.right - wr.left) - (cr.right - cr.left);

    int client_h = MENU_HEIGHT + STATUS_HEIGHT + rows * charHeight;
    int win_h = client_h + frame_h;

    int win_w = (int)((double)win_h * (double)cfg.aspect_w / (double)cfg.aspect_h);

    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    int content_chars = (editor.view_layout == 0) ? (10 + bpr * 3 + 2 + bpr + 2) : (10 + bpr + 2);
    int min_w = content_chars * charWidth + frame_w + 20;

    if (win_w < min_w) win_w = min_w;

    SetWindowPos(hwnd, NULL, wr.left, wr.top, win_w, win_h, SWP_NOZORDER | SWP_NOMOVE);
}

/* ================================================================== */
/* Virtual size                                                        */
/* ================================================================== */

static size_t get_virtual_size(void)
{
    size_t size = get_effective_size(&editor);

    if (!editor.readonly_mode) {
        if (editor.cursor > size) size = editor.cursor;
        if (size < SIZE_MAX) size++;
    }

    return size;
}

static size_t get_total_rows(void)
{
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    size_t size = get_virtual_size();
    size_t rows;

    if (size > SIZE_MAX - (size_t)bpr) rows = SIZE_MAX / (size_t)bpr;
    else rows = (size + (size_t)bpr - 1) / (size_t)bpr;

    if (rows == 0) rows = 1;
    return rows;
}

/* ================================================================== */
/* Custom Dark Mode Scrollbar                                          */
/* ================================================================== */

static RECT g_scrollRect = {0};
static BOOL g_isDraggingScroll = FALSE;
static int g_scrollDragOffset = 0;

static void ClampViewOffset(void) {
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    size_t totalRows = get_total_rows();
    size_t maxScrollRow = totalRows > (size_t)visibleRows ? totalRows - (size_t)visibleRows : 0;
    size_t maxOffset = maxScrollRow * (size_t)bpr;
    if (editor.view_offset > maxOffset) editor.view_offset = maxOffset;
}

static RECT GetScrollThumbRect(void) {
    RECT thumb = {0};
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    size_t totalRows = get_total_rows();
    size_t viewRow = editor.view_offset / (size_t)bpr;
    size_t maxScrollRow = totalRows > (size_t)visibleRows ? totalRows - (size_t)visibleRows : 0;

    if (totalRows <= (size_t)visibleRows) {
        thumb = g_scrollRect;
        return thumb;
    }

    int trackHeight = g_scrollRect.bottom - g_scrollRect.top;
    int thumbHeight = trackHeight * visibleRows / totalRows;
    if (thumbHeight < 20) thumbHeight = 20;
    
    int thumbY = g_scrollRect.top + (trackHeight - thumbHeight) * viewRow / maxScrollRow;

    thumb.left = g_scrollRect.left + 2;
    thumb.right = g_scrollRect.right - 2;
    thumb.top = thumbY;
    thumb.bottom = thumbY + thumbHeight;
    return thumb;
}

static void DrawCustomScrollBar(HDC hdc) {
    if (g_scrollRect.right <= g_scrollRect.left) return;

    HBRUSH trackBrush = CreateSolidBrush(cfg.col_background);
    FillRect(hdc, &g_scrollRect, trackBrush);
    DeleteObject(trackBrush);

    RECT thumb = GetScrollThumbRect();
    HBRUSH thumbBrush = CreateSolidBrush(cfg.col_cursor);
    FillRect(hdc, &thumb, thumbBrush);
    DeleteObject(thumbBrush);
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

    ClampViewOffset();
    InvalidateRect(hwnd, NULL, FALSE);
}

/* ================================================================== */
/* Window layout                                                       */
/* ================================================================== */

static void OnSize(HWND hwnd, int cx, int cy)
{
    fullClientRect.left = 0;
    fullClientRect.top = 0;
    fullClientRect.right = cx;
    fullClientRect.bottom = cy;

    clientRect = fullClientRect;

    int scrollWidth = GetSystemMetrics(SM_CXVSCROLL);
    g_scrollRect.left = cx - scrollWidth;
    g_scrollRect.right = cx;
    g_scrollRect.top = MENU_HEIGHT;
    g_scrollRect.bottom = cy - STATUS_HEIGHT;

    if (g_scrollRect.left < 0) g_scrollRect.left = 0;
    clientRect.right = g_scrollRect.left;

    if (charHeight > 0) {
        visibleRows = (clientRect.bottom - MENU_HEIGHT - STATUS_HEIGHT) / charHeight;
        if (visibleRows < 1) visibleRows = 1;
    }

    ClampViewOffset();
    InvalidateRect(hwnd, NULL, FALSE);
}

/* ================================================================== */
/* Bytes-per-row                                                       */
/* ================================================================== */

static void ClampBPRForLayout(HWND hwnd) {
    int limit = editor.view_layout == 1 ? 48 * 4 : 48;
    if (editor.bytes_per_row > limit) {
        editor.bytes_per_row = limit;
        SnapWindowSize(hwnd, visibleRows);
    }
    ClampViewOffset();
    InvalidateRect(hwnd, NULL, FALSE);
}

static void CycleBPR(HWND hwnd)
{
    static const int values[] = { 8, 16, 24, 32, 48, 64, 96, 128, 192 };
    int limit = editor.view_layout == 1 ? 48 * 4 : 48;
    int current = editor.bytes_per_row;
    int next = 8;

    if (current < limit) {
        next = limit;
        for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
            if (values[i] > current && values[i] <= limit) {
                next = values[i];
                break;
            }
        }
    }

    editor.bytes_per_row = next;
    SnapWindowSize(hwnd, visibleRows);
    ClampViewOffset();
    InvalidateRect(hwnd, NULL, FALSE);
}

/* ================================================================== */
/* Cursor                                                              */
/* ================================================================== */

static void EnsureCursorVisible(void)
{
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    size_t cursor_row = editor.cursor / (size_t)bpr;
    size_t view_row = editor.view_offset / (size_t)bpr;

    if (cursor_row < view_row) {
        editor.view_offset = cursor_row * (size_t)bpr;
    } else if (cursor_row >= view_row + (size_t)visibleRows) {
        editor.view_offset = (cursor_row - (size_t)visibleRows + 1) * (size_t)bpr;
    }
}

/* ================================================================== */
/* Clipboard                                                           */
/* ================================================================== */

static void CopySelectionToClipboard(HWND hwnd)
{
    if (!has_selection(&editor)) return;

    size_t size = get_effective_size(&editor);
    if (size == 0) return;

    size_t low = sel_min(&editor);
    size_t high = sel_max(&editor);

    if (low >= size) return;
    if (high >= size) high = size - 1;
    if (low > high) return;

    size_t count = high - low + 1;
    
    /* Hex mode needs 2 characters per byte. ASCII mode needs 1. */
    size_t multiplier = (editor.edit_mode == 0) ? 2 : 1;
    size_t buffer_size = (count * multiplier) + 1;

    char *buffer = (char *)malloc(buffer_size);
    if (!buffer) return;

    char *p = buffer;

    for (size_t i = low; i <= high; i++) {
        uint8_t byte = get_byte(&editor, i);

        if (editor.edit_mode == 0) {
            /* Hex mode: copy simple hex string without spaces (e.g., 48656C6C6F) */
            sprintf(p, "%02X", byte);
            p += 2;
        } else {
            /* ASCII mode: copy raw text characters */
            *p++ = isprint(byte) ? (char)byte : '.';
        }
    }
    *p = '\0';

    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, buffer_size);
        if (memory) {
            char *destination = (char *)GlobalLock(memory);
            if (destination) {
                memcpy(destination, buffer, buffer_size);
                GlobalUnlock(memory);
                SetClipboardData(CF_TEXT, memory);
            } else {
                GlobalFree(memory);
            }
        }
        CloseClipboard();
    }

    free(buffer);
}

static void PasteFromClipboard(HWND hwnd) {
    if (editor.readonly_mode) return;
    if (!OpenClipboard(hwnd)) return;
    
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) { CloseClipboard(); return; }
    
    char *clipText = (char *)GlobalLock(hData);
    if (!clipText) { CloseClipboard(); return; }
    
    size_t len = strlen(clipText);
    uint8_t *bytes = (uint8_t *)malloc(len / 2 + 1);
    if (!bytes) { GlobalUnlock(hData); CloseClipboard(); return; }
    
    size_t byteCount = 0;
    uint8_t currentByte = 0;
    int nibbleCount = 0;
    
    for (size_t i = 0; i < len; i++) {
        char c = clipText[i];
        int val = -1;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        
        if (val != -1) {
            if (nibbleCount == 0) {
                currentByte = (uint8_t)(val << 4);
                nibbleCount = 1;
            } else {
                currentByte |= (uint8_t)val;
                bytes[byteCount++] = currentByte;
                nibbleCount = 0;
            }
        }
    }
    
    GlobalUnlock(hData);
    CloseClipboard();
    
    if (byteCount == 0) { free(bytes); return; }
    
    size_t paste_offset = editor.cursor;
    if (has_selection(&editor)) {
        paste_offset = sel_min(&editor);
    }
    
    paste_bytes(&editor, paste_offset, bytes, byteCount);
    
    editor.cursor = paste_offset + byteCount;
    clear_selection(&editor);
    EnsureCursorVisible();
    ClampViewOffset();
    InvalidateRect(hwnd, NULL, FALSE);
    
    free(bytes);
}

/* ================================================================== */
/* Save                                                                */
/* ================================================================== */

static void DoSave(HWND hwnd)
{
    if (editor.memory_mode) {
        OPENFILENAMEA ofn = {0};
        char filename[MAX_PATH] = {0};

        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = filename;
        ofn.nMaxFile = sizeof(filename);
        ofn.lpstrFilter = "All Files\0*.*\0";
        ofn.Flags = OFN_OVERWRITEPROMPT;

        if (GetSaveFileNameA(&ofn)) {
            FILE *file = fopen(filename, "wb");

            if (file) {
                if (editor.mem_size > 0) {
                    fwrite(editor.mem_buffer, 1, editor.mem_size, file);
                }
                fclose(file);

                int bpr_keep = editor.bytes_per_row;
                int layout_keep = editor.view_layout;
                int mode_keep = editor.edit_mode;
                int readonly_keep = editor.readonly_mode;
                size_t cursor_keep = editor.cursor;
                size_t view_keep = editor.view_offset;

                cleanup_editor(&editor);

                if (init_file(&editor, filename) != 0) {
                    init_memory_mode(&editor);
                }

                editor.bytes_per_row = bpr_keep;
                editor.view_layout = layout_keep;
                editor.edit_mode = mode_keep;
                editor.readonly_mode = readonly_keep;
                editor.cursor = cursor_keep;
                editor.view_offset = view_keep;

                size_t file_size = get_effective_size(&editor);

                if (file_size == 0) editor.cursor = 0;
                else if (editor.cursor > file_size) editor.cursor = file_size;

                EnsureCursorVisible();
            }
        }
    } else {
        save_dirty(&editor);
    }

    ClampBPRForLayout(hwnd);
}

/* ================================================================== */
/* Open                                                                */
/* ================================================================== */

static void DoOpen(HWND hwnd)
{
    OPENFILENAMEA ofn = {0};
    char filename[MAX_PATH] = {0};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = sizeof(filename);
    ofn.lpstrFilter = "All Files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        cleanup_editor(&editor);

        if (init_file(&editor, filename) != 0) {
            init_memory_mode(&editor);
        }

        editor.bytes_per_row = cfg.bytes_per_row;
        editor.view_layout = cfg.view_layout;
        editor.edit_mode = cfg.edit_mode;
        editor.readonly_mode = 1;
        editor.cursor = 0;
        editor.view_offset = 0;

        clear_selection(&editor);
        hex_state = 0;

        ClampBPRForLayout(hwnd);
    }
}

/* ================================================================== */
/* Button                                                               */
/* ================================================================== */

static RECT GetBtnRect(void)
{
    RECT r;
    r.right = fullClientRect.right - 12;
    r.left = r.right - BTN_W;
    r.bottom = fullClientRect.bottom - 6;
    r.top = r.bottom - BTN_H;

    if (r.left < 0) { r.left = 0; r.right = BTN_W; }
    if (r.top < 0) { r.top = 0; r.bottom = BTN_H; }

    return r;
}

/* ================================================================== */
/* Grid hit testing                                                    */
/* ================================================================== */

static size_t GridHitTest(int x, int y, int *out_mode)
{
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    int xHex = 10 * charWidth;
    int xAscii;

    if (editor.view_layout == 0) xAscii = xHex + (bpr * 3 + 2) * charWidth;
    else xAscii = xHex;

    if (y < MENU_HEIGHT || y >= clientRect.bottom - STATUS_HEIGHT) return (size_t)-1;

    int row = (y - MENU_HEIGHT) / charHeight;
    size_t row_offset = editor.view_offset + (size_t)row * (size_t)bpr;
    int column = -1;

    if (editor.view_layout == 0 && x >= xHex && x < xAscii - 2 * charWidth) {
        column = (x - xHex) / (3 * charWidth);
        if (out_mode) *out_mode = 0;
    } else if (x >= xAscii) {
        column = (x - xAscii) / charWidth;
        if (out_mode) *out_mode = 1;
    }

    if (column >= 0 && column < bpr) {
        size_t offset = row_offset + (size_t)column;
        size_t virtual_size = get_virtual_size();
        if (offset < virtual_size) return offset;
    }

    return (size_t)-1;
}

/* ================================================================== */
/* Menu Hit Testing Fix                                                */
/* ================================================================== */

static int GetMenuBlock(int x, int cw) {
    if (cw <= 0) return -1;
    
    // The menu text starts at x = 15 in WM_PAINT
    int charIndex = (x - 15) / cw;
    if (charIndex < 0) return -1;

    // Exact character lengths of each menu item including trailing space
    static const int menu_lengths[] = { 
        7,  // [O]pen 
        7,  // [S]ave 
        7,  // [U]ndo 
        7,  // [R]edo 
        7,  // [C]opy 
        8,  // [P]aste 
        8,  // [I]nput (Replaces Mode for typing)
        6,  // [B]PR 
        7,  // [M]ode  (Replaces View for layout)
        6   // [X]xit
    };
    int num_items = sizeof(menu_lengths) / sizeof(menu_lengths[0]);
    
    int current_start = 0;
    for (int i = 0; i < num_items; i++) {
        if (charIndex >= current_start && charIndex < current_start + menu_lengths[i]) {
            return i;
        }
        current_start += menu_lengths[i];
    }
    return -1;
}

/* ================================================================== */
/* Window procedure                                                    */
/* ================================================================== */

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;

    switch (msg) {
        case WM_ERASEBKGND: return 1;
        case WM_NCCALCSIZE: if (wParam) return 0; break;
        
        case WM_NCHITTEST: {
            POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &point);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int border = BORDER_PX;

            if (point.y < border) {
                if (point.x < border) return HTTOPLEFT;
                if (point.x > rc.right - border) return HTTOPRIGHT;
                return HTTOP;
            }
            if (point.y > rc.bottom - border) {
                if (point.x < border) return HTBOTTOMLEFT;
                if (point.x > rc.right - border) return HTBOTTOMRIGHT;
                return HTBOTTOM;
            }
            if (point.x < border) return HTLEFT;
            if (point.x > rc.right - border) return HTRIGHT;
            return HTCLIENT;
        }

        case WM_SIZING: {
            if (charHeight == 0) break;
            RECT *rect = (RECT *)lParam;
            RECT wr, cr;
            GetWindowRect(hwnd, &wr);
            GetClientRect(hwnd, &cr);

            int frameHeight = (wr.bottom - wr.top) - (cr.bottom - cr.top);
            int available = (rect->bottom - rect->top) - frameHeight - MENU_HEIGHT - STATUS_HEIGHT;
            int rows = available / charHeight;
            if (rows < 1) rows = 1;
            int snapped = MENU_HEIGHT + STATUS_HEIGHT + rows * charHeight + frameHeight;

            switch (wParam) {
                case WMSZ_TOP: case WMSZ_TOPLEFT: case WMSZ_TOPRIGHT:
                    rect->top = rect->bottom - snapped; return TRUE;
                case WMSZ_BOTTOM: case WMSZ_BOTTOMLEFT: case WMSZ_BOTTOMRIGHT:
                    rect->bottom = rect->top + snapped; return TRUE;
                default: return TRUE;
            }
        }

        case WM_CREATE: {
            DragAcceptFiles(hwnd, TRUE);
            SetTimer(hwnd, 1, (UINT)cfg.menu_hide_delay, NULL);

            hFont = CreateFontA(cfg.font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, cfg.font_name);
            if (!hFont) hFont = (HFONT)GetStockObject(ANSI_FIXED_FONT);

            HDC hdc = GetDC(hwnd);
            SelectObject(hdc, hFont);
            TEXTMETRIC tm;
            GetTextMetrics(hdc, &tm);
            charWidth = tm.tmAveCharWidth;
            charHeight = tm.tmHeight + tm.tmExternalLeading;
            ReleaseDC(hwnd, hdc);

            // REMOVED: SnapWindowSize(hwnd, cfg.rows);
            // This was overriding the 900x900 configured default size.

            RECT current;
            if (GetClientRect(hwnd, &current)) {
                OnSize(hwnd, current.right, current.bottom);
            }

            ClampBPRForLayout(hwnd);
            SetFocus(hwnd);
            break;
        }

        case WM_SIZE: OnSize(hwnd, LOWORD(lParam), HIWORD(lParam)); break;
        case WM_VSCROLL: break;

        case WM_MOUSEMOVE: {
            POINTS points = MAKEPOINTS(lParam);

            if (points.y < MENU_HEIGHT) {
                if (!menu_visible) {
                    menu_visible = TRUE;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                SetTimer(hwnd, 1, (UINT)cfg.menu_hide_delay, NULL);
            }

            if (g_isDraggingScroll && (wParam & MK_LBUTTON)) {
                int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
                size_t totalRows = get_total_rows();
                size_t maxScrollRow = totalRows > (size_t)visibleRows ? totalRows - (size_t)visibleRows : 0;
                
                int trackHeight = g_scrollRect.bottom - g_scrollRect.top;
                RECT thumb = GetScrollThumbRect();
                int thumbHeight = thumb.bottom - thumb.top;
                int availableTrack = trackHeight - thumbHeight;
                
                if (availableTrack > 0 && maxScrollRow > 0) {
                    int newY = points.y - g_scrollDragOffset;
                    int clampedY = max(g_scrollRect.top, min(newY, g_scrollRect.bottom - thumbHeight));
                    size_t newRow = (size_t)((clampedY - g_scrollRect.top) * maxScrollRow / availableTrack);
                    editor.view_offset = newRow * (size_t)bpr;
                }
                ClampViewOffset();
                InvalidateRect(hwnd, NULL, FALSE);
            }

            if (is_dragging && (wParam & MK_LBUTTON)) {
                int mode;
                size_t offset = GridHitTest(points.x, points.y, &mode);
                if (offset != (size_t)-1) {
                    editor.selection_end = offset;
                    editor.cursor = offset;
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

            HDC memoryDC = CreateCompatibleDC(hdc);
            HBITMAP memoryBitmap = CreateCompatibleBitmap(hdc, paintRect.right, paintRect.bottom);

            if (!memoryDC || !memoryBitmap) {
                if (memoryDC) DeleteDC(memoryDC);
                if (memoryBitmap) DeleteObject(memoryBitmap);
                EndPaint(hwnd, &ps);
                return 0;
            }

            HBITMAP oldBitmap = (HBITMAP)SelectObject(memoryDC, memoryBitmap);
            HBRUSH backgroundBrush = CreateSolidBrush(cfg.col_background);
            FillRect(memoryDC, &paintRect, backgroundBrush);
            DeleteObject(backgroundBrush);

            SelectObject(memoryDC, hFont);
            SetBkMode(memoryDC, TRANSPARENT);

            RECT menuRect = { 0, 0, paintRect.right, MENU_HEIGHT };
            HBRUSH menuBrush = CreateSolidBrush(cfg.col_menu_bg);
            FillRect(memoryDC, &menuRect, menuBrush);
            DeleteObject(menuBrush);

            if (menu_visible) {
                SetTextColor(memoryDC, cfg.col_menu_text);
                const char *menuText = "[O]pen [S]ave [U]ndo [R]edo [C]opy [P]aste [I]nput [B]PR [M]ode [X]xit";
                TextOutA(memoryDC, 15, 8, menuText, (int)strlen(menuText));
            } else {
                SetTextColor(memoryDC, cfg.col_menu_hidden);
                const char *filename = editor.filename;
                const char *slash1 = strrchr(filename, '\\');
                const char *slash2 = strrchr(filename, '/');

                if (slash1 || slash2) filename = slash1 > slash2 ? slash1 + 1 : slash2 + 1;

                char title[512];
                snprintf(title, sizeof(title), "RAM-Only Hex Editor [%s]", filename);
                TextOutA(memoryDC, 15, 8, title, (int)strlen(title));
            }

            int y = MENU_HEIGHT;
            int xHex = 10 * charWidth;
            size_t virtual_size = get_virtual_size();
            int selectionActive = has_selection(&editor);
            size_t selectionLow = selectionActive ? sel_min(&editor) : 0;
            size_t selectionHigh = selectionActive ? sel_max(&editor) : 0;

            for (int row = 0; row < visibleRows; row++) {
                size_t offset = editor.view_offset + (size_t)row * (size_t)bpr;
                if (offset >= virtual_size) break;

                SetTextColor(memoryDC, cfg.col_offset);
                char offsetText[32];
                sprintf(offsetText, "%08llX  ", (unsigned long long)offset);
                TextOutA(memoryDC, 0, y, offsetText, (int)strlen(offsetText));

                int xAscii;

                if (editor.view_layout == 0) {
                    xAscii = xHex + (bpr * 3 + 2) * charWidth;

                    for (int column = 0; column < bpr; column++) {
                        size_t current = offset + (size_t)column;
                        if (current >= virtual_size) break;

                        int selected = selectionActive && current >= selectionLow && current <= selectionHigh;
                        int cursor = current == editor.cursor && editor.edit_mode == 0;

                        if (selected) {
                            RECT selectionRect = { xHex + column * 3 * charWidth, y, xHex + (column + 1) * 3 * charWidth, y + charHeight };
                            HBRUSH brush = CreateSolidBrush(cfg.col_selection);
                            FillRect(memoryDC, &selectionRect, brush);
                            DeleteObject(brush);
                        }

                        char hexText[4];
                        sprintf(hexText, "%02X ", get_byte(&editor, current));

                        if (cursor) {
                            RECT cursorRect = {
                                xHex + column * 3 * charWidth,
                                y,
                                xHex + (column + 1) * 3 * charWidth,
                                y + charHeight
                            };
                            HBRUSH brush = CreateSolidBrush(cfg.col_cursor);
                            FillRect(memoryDC, &cursorRect, brush);
                            DeleteObject(brush);
                            SetTextColor(memoryDC, RGB(0, 0, 0));
                        } else {
                            SetTextColor(memoryDC, cfg.col_hex);
                        }

                        TextOutA(memoryDC, xHex + column * 3 * charWidth, y, hexText, 3);
                    }
                } else {
                    xAscii = xHex;
                }

                for (int column = 0; column < bpr; column++) {
                    size_t current = offset + (size_t)column;
                    if (current >= virtual_size) break;

                    int selected = selectionActive && current >= selectionLow && current <= selectionHigh;
                    int cursor = current == editor.cursor && editor.edit_mode == 1;

                    if (selected) {
                        RECT selectionRect = { xAscii + column * charWidth, y, xAscii + (column + 1) * charWidth, y + charHeight };
                        HBRUSH brush = CreateSolidBrush(cfg.col_selection);
                        FillRect(memoryDC, &selectionRect, brush);
                        DeleteObject(brush);
                    }

                    uint8_t byte = get_byte(&editor, current);
                    char character = isprint(byte) ? (char)byte : '.';

                    if (cursor) {
                        RECT cursorRect = { xAscii + column * charWidth, y, xAscii + (column + 1) * charWidth, y + charHeight };
                        HBRUSH brush = CreateSolidBrush(cfg.col_cursor);
                        FillRect(memoryDC, &cursorRect, brush);
                        DeleteObject(brush);
                        SetTextColor(memoryDC, RGB(0, 0, 0));
                    } else {
                        SetTextColor(memoryDC, cfg.col_ascii);
                    }

                    TextOutA(memoryDC, xAscii + column * charWidth, y, &character, 1);
                }
                y += charHeight;
            }

            RECT statusRect = { 0, paintRect.bottom - STATUS_HEIGHT, paintRect.right, paintRect.bottom };
            HBRUSH statusBrush = CreateSolidBrush(cfg.col_status_bg);
            FillRect(memoryDC, &statusRect, statusBrush);
            DeleteObject(statusBrush);

            SetTextColor(memoryDC, cfg.col_status_text);
            char statusText[300];
            sprintf(statusText, " Size: %llu | Off: %08llX | Mode: %s | Layout: %s | BPR: %d | Dirty: %llu%s",
                (unsigned long long)get_effective_size(&editor),
                (unsigned long long)editor.cursor,
                editor.edit_mode == 0 ? "HEX" : "TEXT",
                editor.view_layout == 0 ? "HEX+TXT" : "TXT ONLY",
                editor.bytes_per_row,
                (unsigned long long)editor.tracker.count,
                editor.memory_mode ? " | MEM" : "");

            TextOutA(memoryDC, 10, paintRect.bottom - 20, statusText, (int)strlen(statusText));

            RECT buttonRect = GetBtnRect();
            HBRUSH buttonBrush = CreateSolidBrush(editor.readonly_mode ? cfg.col_ro_btn : cfg.col_rw_btn);
            FillRect(memoryDC, &buttonRect, buttonBrush);
            DeleteObject(buttonBrush);

            SetTextColor(memoryDC, RGB(255, 255, 255));
            const char *buttonText = editor.readonly_mode ? "RO" : "RW";
            DrawTextA(memoryDC, buttonText, -1, &buttonRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            DrawCustomScrollBar(memoryDC);

            BitBlt(hdc, 0, 0, paintRect.right, paintRect.bottom, memoryDC, 0, 0, SRCCOPY);
            SelectObject(memoryDC, oldBitmap);
            DeleteObject(memoryBitmap);
            DeleteDC(memoryDC);
            EndPaint(hwnd, &ps);
            break;
        }

        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            POINT point = { x, y };
            SetFocus(hwnd);

            if (PtInRect(&g_scrollRect, point)) {
                RECT thumb = GetScrollThumbRect();
                if (PtInRect(&thumb, point)) {
                    g_isDraggingScroll = TRUE;
                    g_scrollDragOffset = point.y - thumb.top;
                    SetCapture(hwnd);
                } else {
                    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
                    size_t page = (size_t)visibleRows * bpr;
                    if (point.y < thumb.top) {
                        if (editor.view_offset >= page) editor.view_offset -= page;
                        else editor.view_offset = 0;
                    } else {
                        editor.view_offset += page;
                    }
                    ClampViewOffset();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }

            RECT buttonRect = GetBtnRect();
            if (PtInRect(&buttonRect, point)) {
                editor.readonly_mode = 1 - editor.readonly_mode;
                hex_state = 0;
                if (editor.readonly_mode) {
                    size_t size = get_effective_size(&editor);
                    if (size == 0) editor.cursor = 0;
                    else if (editor.cursor >= size) editor.cursor = size - 1;
                    clear_selection(&editor);
                    EnsureCursorVisible();
                }
                ClampViewOffset();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            if (y < MENU_HEIGHT) {
                if (menu_visible) {
                    int block = GetMenuBlock(x, charWidth);
                    switch (block) {
                        case 0: DoOpen(hwnd); break;
                        case 1: DoSave(hwnd); break;
                        case 2: undo(&editor); EnsureCursorVisible(); ClampViewOffset(); InvalidateRect(hwnd, NULL, FALSE); break;
                        case 3: redo(&editor); EnsureCursorVisible(); ClampViewOffset(); InvalidateRect(hwnd, NULL, FALSE); break;
                        case 4: CopySelectionToClipboard(hwnd); break;
                        case 5: PasteFromClipboard(hwnd); break;
                        case 6: editor.edit_mode = 1 - editor.edit_mode; hex_state = 0; break;
                        case 7: CycleBPR(hwnd); break;
                        case 8: editor.view_layout = 1 - editor.view_layout; ClampBPRForLayout(hwnd); break;
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
            size_t offset = GridHitTest(x, y, &mode);
            if (offset != (size_t)-1) {
                editor.edit_mode = mode;
                hex_state = 0;
                if (GetKeyState(VK_SHIFT) & 0x8000) {
                    if (editor.selection_start == (size_t)-1) editor.selection_start = editor.cursor;
                    editor.selection_end = offset;
                } else {
                    clear_selection(&editor);
                    editor.selection_start = offset;
                    editor.selection_end = offset;
                    is_dragging = 1;
                    SetCapture(hwnd);
                }
                editor.cursor = offset;
                EnsureCursorVisible();
                ClampViewOffset();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }

        case WM_LBUTTONUP:
            if (g_isDraggingScroll) {
                g_isDraggingScroll = FALSE;
                ReleaseCapture();
            }
            if (is_dragging) {
                is_dragging = 0;
                ReleaseCapture();
                if (editor.selection_start == editor.selection_end) clear_selection(&editor);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;

        case WM_MOUSEWHEEL: WheelScroll(hwnd, GET_WHEEL_DELTA_WPARAM(wParam)); break;

        case WM_DROPFILES: {
            HDROP drop = (HDROP)wParam;
            char filename[MAX_PATH];
            DragQueryFileA(drop, 0, filename, MAX_PATH);
            cleanup_editor(&editor);

            if (init_file(&editor, filename) != 0) init_memory_mode(&editor);

            editor.bytes_per_row = cfg.bytes_per_row;
            editor.view_layout = cfg.view_layout;
            editor.edit_mode = cfg.edit_mode;
            editor.readonly_mode = 0;
            editor.cursor = 0;
            editor.view_offset = 0;
            clear_selection(&editor);
            hex_state = 0;
            DragFinish(drop);
            ClampBPRForLayout(hwnd);
            break;
        }

        case WM_KEYDOWN: {
            int shift = GetKeyState(VK_SHIFT) & 0x8000;
            int ctrl = GetKeyState(VK_CONTROL) & 0x8000;

            if (ctrl && wParam == 'C') { CopySelectionToClipboard(hwnd); break; }
            if (ctrl && wParam == 'V') { PasteFromClipboard(hwnd); break; }
            if (ctrl && wParam == 'Z') { undo(&editor); EnsureCursorVisible(); ClampViewOffset(); InvalidateRect(hwnd, NULL, FALSE); break; }
            if (ctrl && wParam == 'Y') { redo(&editor); EnsureCursorVisible(); ClampViewOffset(); InvalidateRect(hwnd, NULL, FALSE); break; }
            if (ctrl && wParam == 'S') { DoSave(hwnd); break; }

            if (shift && editor.selection_start == (size_t)-1) {
                editor.selection_start = editor.cursor;
                editor.selection_end = editor.cursor;
            }

            size_t old_cursor = editor.cursor;
            size_t virtual_size = get_virtual_size();
            int can_extend = !editor.readonly_mode;
            size_t page = (size_t)bpr * (size_t)visibleRows;

            switch (wParam) {
                case VK_UP: if (editor.cursor >= (size_t)bpr) editor.cursor -= (size_t)bpr; break;
                case VK_DOWN:
                    if (can_extend && editor.memory_mode) editor.cursor += (size_t)bpr;
                    else if (editor.cursor + (size_t)bpr < virtual_size) editor.cursor += (size_t)bpr;
                    break;
                case VK_LEFT: if (editor.cursor > 0) editor.cursor--; break;
                case VK_RIGHT:
                    if (can_extend && editor.memory_mode) editor.cursor++;
                    else if (virtual_size > 0 && editor.cursor < virtual_size - 1) editor.cursor++;
                    break;
                case VK_PRIOR: if (editor.cursor >= page) editor.cursor -= page; else editor.cursor = 0; break;
                case VK_NEXT:
                    if (can_extend && editor.memory_mode) editor.cursor += page;
                    else if (editor.cursor + page < virtual_size) editor.cursor += page;
                    else if (virtual_size > 0) editor.cursor = virtual_size - 1;
                    break;
                case VK_HOME: editor.cursor = 0; break;
                case VK_END: if (virtual_size > 0) editor.cursor = virtual_size - 1; break;
                case VK_F2: DoSave(hwnd); break;
                case VK_F4: editor.view_layout = 1 - editor.view_layout; ClampBPRForLayout(hwnd); break;
                default: break;
            }

            if (shift) editor.selection_end = editor.cursor;
            else if (editor.cursor != old_cursor) clear_selection(&editor);

            if (editor.cursor != old_cursor) hex_state = 0;

            EnsureCursorVisible();
            ClampViewOffset();
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }

        case WM_CHAR: {
            if (editor.readonly_mode) break;

            if (editor.edit_mode == 0) {
                int value = -1;
                char character = (char)wParam;

                if (character >= '0' && character <= '9') value = character - '0';
                else if (character >= 'a' && character <= 'f') value = character - 'a' + 10;
                else if (character >= 'A' && character <= 'F') value = character - 'A' + 10;

                if (value != -1) {
                    if (hex_state == 0) {
                        temp_hex = (uint8_t)(value << 4);
                        hex_state = 1;
                    } else {
                        uint8_t byte = temp_hex | (uint8_t)value;
                        set_byte(&editor, editor.cursor, byte);
                        hex_state = 0;

                        size_t new_size = get_effective_size(&editor);
                        if (editor.memory_mode) editor.cursor++;
                        else if (editor.cursor < new_size) editor.cursor++;

                        EnsureCursorVisible();
                        ClampViewOffset();
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
            } else {
                if (isprint((int)wParam)) {
                    set_byte(&editor, editor.cursor, (uint8_t)wParam);

                    size_t new_size = get_effective_size(&editor);
                    if (editor.memory_mode) editor.cursor++;
                    else if (editor.cursor < new_size) editor.cursor++;

                    EnsureCursorVisible();
                    ClampViewOffset();
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
/* WinMain                                                             */
/* ================================================================== */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevious, LPSTR commandLine, int showCommand)
{
    (void)hPrevious;
    (void)commandLine;

    load_config();
    memset(&editor, 0, sizeof(editor));

    if (__argc > 1) {
        if (init_file(&editor, __argv[1]) != 0) init_memory_mode(&editor);
    } else {
        init_memory_mode(&editor);
    }

    editor.bytes_per_row = cfg.bytes_per_row;
    editor.view_layout = cfg.view_layout;
    editor.edit_mode = cfg.edit_mode;
    editor.readonly_mode = 0;

    WNDCLASSEXA windowClass = {0};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = hInstance;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.hbrBackground = NULL;
    windowClass.lpszClassName = "HexEditorClass";

    RegisterClassExA(&windowClass);

    HWND hwnd = CreateWindowExA(
        WS_EX_ACCEPTFILES | WS_EX_COMPOSITED,
        "HexEditorClass",
        "Hex Editor",
        WS_POPUP | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT,
        cfg.window_width,  // Uses 900 from config
        cfg.window_height, // Uses 900 from config
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) return 1;

    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG message;
    while (GetMessage(&message, NULL, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    return (int)message.wParam;
}