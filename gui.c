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
#include "hex.h"

/* ================================================================== */
/*  Layout Constants                                                   */
/* ================================================================== */

#define MENU_HEIGHT     35
#define STATUS_HEIGHT   28
#define BTN_W           64
#define BTN_H           20
#define BORDER_PX       5

/* ================================================================== */
/*  Configuration (loaded from / written to config.ini)                */
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
    int      menu_hide_delay;
    int      bytes_per_row;
    int      view_layout;
    int      edit_mode;
    int      tracker_cap;
} AppConfig;

static AppConfig cfg;
static char      ini_path[MAX_PATH];

/* ---- INI helpers ---- */

static COLORREF ini_get_color(const char *sec, const char *key,
                              COLORREF def)
{
    char buf[64] = {0};
    GetPrivateProfileStringA(sec, key, "", buf, sizeof(buf), ini_path);
    int r, g, b;
    if (sscanf(buf, "%d,%d,%d", &r, &g, &b) == 3)
        return RGB(r, g, b);
    return def;
}

static void ini_put_color(const char *sec, const char *key, COLORREF c)
{
    char buf[64];
    sprintf(buf, "%d,%d,%d", GetRValue(c), GetGValue(c), GetBValue(c));
    WritePrivateProfileStringA(sec, key, buf, ini_path);
}

static void generate_default_ini(void)
{
    WritePrivateProfileStringA("Window", "rows",       "16",  ini_path);
    WritePrivateProfileStringA("Window", "aspect_w",   "16",  ini_path);
    WritePrivateProfileStringA("Window", "aspect_h",   "9",   ini_path);

    WritePrivateProfileStringA("Font",   "name",       "Consolas", ini_path);
    WritePrivateProfileStringA("Font",   "size",       "16",  ini_path);

    ini_put_color("Colors", "background",    RGB(30,30,30));
    ini_put_color("Colors", "menu_bg",       RGB(45,45,45));
    ini_put_color("Colors", "menu_text",     RGB(220,220,220));
    ini_put_color("Colors", "menu_hidden",   RGB(100,100,100));
    ini_put_color("Colors", "offset",        RGB(0,255,255));
    ini_put_color("Colors", "hex",           RGB(0,255,0));
    ini_put_color("Colors", "ascii",         RGB(255,255,0));
    ini_put_color("Colors", "selection",     RGB(0,50,150));
    ini_put_color("Colors", "cursor",        RGB(0,150,255));
    ini_put_color("Colors", "status_bg",     RGB(40,40,40));
    ini_put_color("Colors", "status_text",   RGB(180,180,180));
    ini_put_color("Colors", "readonly_btn",  RGB(0,180,0));
    ini_put_color("Colors", "overwrite_btn", RGB(200,0,0));

    WritePrivateProfileStringA("Behavior", "menu_hide_delay", "2000", ini_path);
    WritePrivateProfileStringA("Behavior", "bytes_per_row",   "16",   ini_path);
    WritePrivateProfileStringA("Behavior", "view_layout",     "0",    ini_path);
    WritePrivateProfileStringA("Behavior", "edit_mode",       "0",    ini_path);

    WritePrivateProfileStringA("Engine", "tracker_initial_capacity",
                               "65536", ini_path);
}

static void load_config(void)
{
    /* Resolve path: <exe_dir>\config.ini */
    char exe_dir[MAX_PATH];
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *slash = strrchr(exe_dir, '\\');
    if (slash) *(slash + 1) = '\0';
    snprintf(ini_path, MAX_PATH, "%sconfig.ini", exe_dir);

    if (GetFileAttributesA(ini_path) == INVALID_FILE_ATTRIBUTES)
        generate_default_ini();

    cfg.rows          = GetPrivateProfileIntA("Window","rows",16,ini_path);
    cfg.aspect_w      = GetPrivateProfileIntA("Window","aspect_w",16,ini_path);
    cfg.aspect_h      = GetPrivateProfileIntA("Window","aspect_h",9,ini_path);
    GetPrivateProfileStringA("Font","name","Consolas",
                             cfg.font_name,sizeof(cfg.font_name),ini_path);
    cfg.font_size     = GetPrivateProfileIntA("Font","size",16,ini_path);

    cfg.col_background  = ini_get_color("Colors","background",   RGB(30,30,30));
    cfg.col_menu_bg     = ini_get_color("Colors","menu_bg",      RGB(45,45,45));
    cfg.col_menu_text   = ini_get_color("Colors","menu_text",    RGB(220,220,220));
    cfg.col_menu_hidden = ini_get_color("Colors","menu_hidden",  RGB(100,100,100));
    cfg.col_offset      = ini_get_color("Colors","offset",       RGB(0,255,255));
    cfg.col_hex         = ini_get_color("Colors","hex",          RGB(0,255,0));
    cfg.col_ascii       = ini_get_color("Colors","ascii",        RGB(255,255,0));
    cfg.col_selection   = ini_get_color("Colors","selection",    RGB(0,50,150));
    cfg.col_cursor      = ini_get_color("Colors","cursor",       RGB(0,150,255));
    cfg.col_status_bg   = ini_get_color("Colors","status_bg",    RGB(40,40,40));
    cfg.col_status_text = ini_get_color("Colors","status_text",  RGB(180,180,180));
    cfg.col_ro_btn      = ini_get_color("Colors","readonly_btn", RGB(0,180,0));
    cfg.col_rw_btn      = ini_get_color("Colors","overwrite_btn",RGB(200,0,0));

    cfg.menu_hide_delay = GetPrivateProfileIntA("Behavior","menu_hide_delay",
                                                2000,ini_path);
    cfg.bytes_per_row   = GetPrivateProfileIntA("Behavior","bytes_per_row",
                                                16,ini_path);
    cfg.view_layout     = GetPrivateProfileIntA("Behavior","view_layout",
                                                0,ini_path);
    cfg.edit_mode       = GetPrivateProfileIntA("Behavior","edit_mode",
                                                0,ini_path);
    cfg.tracker_cap     = GetPrivateProfileIntA("Engine",
                                                "tracker_initial_capacity",
                                                65536,ini_path);
}

/* ================================================================== */
/*  Global State                                                       */
/* ================================================================== */

static HexEditor editor;
static int       hex_state   = 0;
static uint8_t   temp_hex    = 0;
static HFONT     hFont       = NULL;
static int       charWidth   = 8;
static int       charHeight  = 19;
static int       visibleRows = 16;
static RECT      clientRect;
static BOOL      menu_visible = TRUE;
static int       is_dragging  = 0;

/* ================================================================== */
/*  Snap window height to exact row count + aspect-ratio width         */
/* ================================================================== */

static void SnapWindowSize(HWND hwnd, int rows)
{
    if (charHeight == 0) return;

    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);

    int frame_h = (wr.bottom - wr.top) - (cr.bottom - cr.top);
    int frame_w = (wr.right  - wr.left) - (cr.right  - cr.left);

    int client_h = MENU_HEIGHT + STATUS_HEIGHT + rows * charHeight;
    int win_h    = client_h + frame_h;

    /* Width from aspect ratio */
    int win_w = (int)((double)win_h * cfg.aspect_w / cfg.aspect_h);

    /* Ensure minimum width for hex display */
    int bpr  = editor.bytes_per_row;
    int min_w = (10 + bpr * 3 + 2 + bpr + 2) * charWidth + frame_w + 20;
    if (win_w < min_w) win_w = min_w;

    SetWindowPos(hwnd, NULL, wr.left, wr.top, win_w, win_h,
                 SWP_NOZORDER | SWP_NOMOVE);
}

/* ================================================================== */
/*  Clipboard: CopySelectionToClipboard                                */
/*  Format per row:  OOOOOOOO <hex continuous> <ascii continuous>      */
/*  Unselected bytes padded with spaces.                               */
/* ================================================================== */

static void CopySelectionToClipboard(HWND hwnd)
{
    if (!has_selection(&editor)) return;

    size_t lo = sel_min(&editor);
    size_t hi = sel_max(&editor);
    int    bpr = editor.bytes_per_row;
    size_t esize = get_effective_size(&editor);
    if (hi >= esize) hi = esize - 1;

    size_t row_first = (lo / (size_t)bpr) * (size_t)bpr;
    size_t row_last  = (hi / (size_t)bpr) * (size_t)bpr;
    size_t total_rows = (row_last - row_first) / (size_t)bpr + 1;

    /* Allocate: per row ≈ 8 + 1 + bpr*2 + 1 + bpr + 2 */
    size_t per_row = 12 + (size_t)bpr * 3 + 4;
    char  *buf = (char *)malloc(total_rows * per_row + 1);
    if (!buf) return;
    char *p = buf;

    for (size_t row_off = row_first; row_off <= row_last;
         row_off += (size_t)bpr)
    {
        p += sprintf(p, "%08llX ", (unsigned long long)row_off);

        /* Hex column */
        for (int j = 0; j < bpr; j++) {
            size_t off = row_off + (size_t)j;
            if (off >= lo && off <= hi && off < esize)
                p += sprintf(p, "%02X", get_byte(&editor, off));
            else
                p += sprintf(p, "  ");
        }

        *p++ = ' ';

        /* ASCII column */
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

/* ================================================================== */
/*  Save helper (handles both memory-mode and file-mode)               */
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
                /* Switch to file-backed mode */
                if (editor.mem_buffer) { free(editor.mem_buffer); editor.mem_buffer = NULL; }
                editor.memory_mode = 0;
                init_file(&editor, fn);
            }
        }
    } else {
        save_dirty(&editor);
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

/* ================================================================== */
/*  Open File helper                                                   */
/* ================================================================== */

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
        editor.bytes_per_row = cfg.bytes_per_row;
        editor.view_layout   = cfg.view_layout;
        editor.edit_mode     = cfg.edit_mode;
        editor.readonly_mode = 0;
        init_file(&editor, fn);
        editor.cursor      = 0;
        editor.view_offset = 0;
        clear_selection(&editor);
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

/* ================================================================== */
/*  Hit-test: is (x,y) inside the RO/RW button?                       */
/* ================================================================== */

static RECT GetBtnRect(void)
{
    RECT r;
    r.right  = clientRect.right - 8;
    r.left   = r.right - BTN_W;
    r.bottom = clientRect.bottom - 4;
    r.top    = r.bottom - BTN_H;
    return r;
}

/* ================================================================== */
/*  Ensure cursor is visible (auto-scroll)                             */
/* ================================================================== */

static void EnsureCursorVisible(void)
{
    int bpr = editor.bytes_per_row;
    size_t cursor_row  = editor.cursor / (size_t)bpr;
    size_t view_row    = editor.view_offset / (size_t)bpr;
    if (cursor_row < view_row)
        editor.view_offset = cursor_row * (size_t)bpr;
    else if (cursor_row >= view_row + (size_t)visibleRows)
        editor.view_offset = (cursor_row - (size_t)visibleRows + 1) * (size_t)bpr;
}

/* ================================================================== */
/*  Grid hit-test: returns byte offset or (size_t)-1                   */
/* ================================================================== */

static size_t GridHitTest(int xPos, int yPos, int *out_mode)
{
    int bpr  = editor.bytes_per_row;
    int xHex = 10 * charWidth;
    int xAscii = (editor.view_layout == 0)
                     ? xHex + (bpr * 3 + 2) * charWidth
                     : xHex;

    if (yPos < MENU_HEIGHT || yPos >= clientRect.bottom - STATUS_HEIGHT)
        return (size_t)-1;

    int row = (yPos - MENU_HEIGHT) / charHeight;
    size_t row_off = editor.view_offset + (size_t)row * (size_t)bpr;
    int col = -1;

    if (editor.view_layout == 0 &&
        xPos >= xHex && xPos < xAscii - 2 * charWidth) {
        col = (xPos - xHex) / (3 * charWidth);
        if (out_mode) *out_mode = 0;
    } else if (xPos >= xAscii) {
        col = (xPos - xAscii) / charWidth;
        if (out_mode) *out_mode = 1;
    }

    if (col >= 0 && col < bpr) {
        size_t off = row_off + (size_t)col;
        if (off < get_effective_size(&editor) || editor.memory_mode)
            return off;
    }
    return (size_t)-1;
}

/* ================================================================== */
/*  Window Procedure                                                   */
/* ================================================================== */

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int bpr = editor.bytes_per_row;

    switch (msg) {

    /* ---- Prevent background erase (flicker-free scrolling) ---- */
    case WM_ERASEBKGND:
        return 1;

    /* ---- Borderless but resizable: strip non-client area ---- */
    case WM_NCCALCSIZE:
        if (wParam) return 0;
        break;

    /* ---- Provide resize borders for borderless window ---- */
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);
        RECT rc; GetClientRect(hwnd, &rc);
        int b = BORDER_PX;
        if (pt.y < b) {
            if      (pt.x < b)            return HTTOPLEFT;
            else if (pt.x > rc.right - b) return HTTOPRIGHT;
            else                          return HTTOP;
        }
        if (pt.y > rc.bottom - b) {
            if      (pt.x < b)            return HTBOTTOMLEFT;
            else if (pt.x > rc.right - b) return HTBOTTOMRIGHT;
            else                          return HTBOTTOM;
        }
        if (pt.x < b)            return HTLEFT;
        if (pt.x > rc.right - b) return HTRIGHT;
        return HTCLIENT;
    }

    /* ---- Constrain height to row-aligned pixels on manual resize ---- */
    case WM_SIZING: {
        RECT *r = (RECT *)lParam;
        RECT wr, cr;
        GetWindowRect(hwnd, &wr);
        GetClientRect(hwnd, &cr);
        int fh = (wr.bottom - wr.top) - (cr.bottom - cr.top);
        int avail = (r->bottom - r->top) - fh
                    - MENU_HEIGHT - STATUS_HEIGHT;
        int rows = avail / charHeight;
        if (rows < 1) rows = 1;
        int snapped = MENU_HEIGHT + STATUS_HEIGHT
                      + rows * charHeight + fh;
        switch (wParam) {
            case WMSZ_TOP:
            case WMSZ_TOPLEFT:
            case WMSZ_TOPRIGHT:
                r->top = r->bottom - snapped;
                break;
            default:
                r->bottom = r->top + snapped;
                break;
        }
        return TRUE;
    }

    /* ---- Creation ---- */
    case WM_CREATE:
        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, 1, (UINT)cfg.menu_hide_delay, NULL);
        hFont = CreateFontA(cfg.font_size, 0, 0, 0, FW_NORMAL,
                            FALSE, FALSE, FALSE, ANSI_CHARSET,
                            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY,
                            FIXED_PITCH | FF_MODERN, cfg.font_name);
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
        SetFocus(hwnd);
        break;

    /* ---- Size ---- */
    case WM_SIZE:
        clientRect.right  = LOWORD(lParam);
        clientRect.bottom = HIWORD(lParam);
        visibleRows = (clientRect.bottom - MENU_HEIGHT - STATUS_HEIGHT)
                      / charHeight;
        if (visibleRows < 1) visibleRows = 1;
        break;

    /* ---- Mouse move: menu reveal + drag-selection ---- */
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
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        break;
    }

    /* ---- Timer: hide menu ---- */
    case WM_TIMER:
        if (wParam == 1 && menu_visible) {
            menu_visible = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;

    /* ---- Paint (double-buffered) ---- */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(
                             hdc, clientRect.right, clientRect.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        /* 1. Background */
        HBRUSH bgBr = CreateSolidBrush(cfg.col_background);
        FillRect(memDC, &clientRect, bgBr);
        DeleteObject(bgBr);
        SelectObject(memDC, hFont);
        SetBkMode(memDC, TRANSPARENT);

        /* 2. Top menu / title bar */
        RECT topR = {0, 0, clientRect.right, MENU_HEIGHT};
        HBRUSH menuBr = CreateSolidBrush(cfg.col_menu_bg);
        FillRect(memDC, &topR, menuBr);
        DeleteObject(menuBr);

        if (menu_visible) {
            SetTextColor(memDC, cfg.col_menu_text);
            const char *menuTxt =
                "[O]pen  [S]ave  [M]ode  [B]PR  [V]iew  [X]xit";
            TextOutA(memDC, 15, 8, menuTxt, (int)strlen(menuTxt));
        } else {
            SetTextColor(memDC, cfg.col_menu_hidden);
            const char *fname = editor.filename;
            const char *p1 = strrchr(fname, '\\');
            const char *p2 = strrchr(fname, '/');
            if (p1 || p2)
                fname = (p1 > p2) ? p1 + 1 : p2 + 1;
            char title[512];
            snprintf(title, sizeof(title),
                     "RAM-Only Hex Editor [%s]", fname);
            TextOutA(memDC, 15, 8, title, (int)strlen(title));
        }

        /* 3. Hex / ASCII grid */
        int y     = MENU_HEIGHT;
        int xHex  = 10 * charWidth;
        size_t esize = get_effective_size(&editor);

        int selActive = has_selection(&editor);
        size_t sLo = selActive ? sel_min(&editor) : 0;
        size_t sHi = selActive ? sel_max(&editor) : 0;

        for (int i = 0; i < visibleRows; i++) {
            size_t offset = editor.view_offset + (size_t)i * (size_t)bpr;
            if (offset >= esize && esize > 0) break;

            /* Offset column */
            SetTextColor(memDC, cfg.col_offset);
            char offStr[16];
            sprintf(offStr, "%08llX  ", (unsigned long long)offset);
            TextOutA(memDC, 0, y, offStr, (int)strlen(offStr));

            int xAscii;

            if (editor.view_layout == 0) {
                /* Hex + ASCII */
                xAscii = xHex + (bpr * 3 + 2) * charWidth;

                for (int j = 0; j < bpr; j++) {
                    size_t off = offset + (size_t)j;
                    if (off >= esize && esize > 0) break;

                    int isSel = selActive && off >= sLo && off <= sHi;
                    int isCur = (off == editor.cursor &&
                                 editor.edit_mode == 0);

                    /* Selection highlight */
                    if (isSel) {
                        RECT r = {xHex + j*3*charWidth, y,
                                  xHex + (j+1)*3*charWidth, y + charHeight};
                        HBRUSH sb = CreateSolidBrush(cfg.col_selection);
                        FillRect(memDC, &r, sb);
                        DeleteObject(sb);
                    }

                    char hexStr[4];
                    sprintf(hexStr, "%02X ", get_byte(&editor, off));

                    if (isCur) {
                        RECT r = {xHex + j*3*charWidth, y,
                                  xHex + (j+1)*3*charWidth, y + charHeight};
                        HBRUSH cb = CreateSolidBrush(cfg.col_cursor);
                        FillRect(memDC, &r, cb);
                        DeleteObject(cb);
                        SetTextColor(memDC, RGB(0,0,0));
                    } else {
                        SetTextColor(memDC, cfg.col_hex);
                    }
                    TextOutA(memDC, xHex + j*3*charWidth, y, hexStr, 3);
                }
            } else {
                xAscii = xHex;
            }

            /* ASCII column */
            for (int j = 0; j < bpr; j++) {
                size_t off = offset + (size_t)j;
                if (off >= esize && esize > 0) break;

                int isSel = selActive && off >= sLo && off <= sHi;
                int isCur = (off == editor.cursor && editor.edit_mode == 1);

                if (isSel) {
                    RECT r = {xAscii + j*charWidth, y,
                              xAscii + (j+1)*charWidth, y + charHeight};
                    HBRUSH sb = CreateSolidBrush(cfg.col_selection);
                    FillRect(memDC, &r, sb);
                    DeleteObject(sb);
                }

                uint8_t c = get_byte(&editor, off);
                char ch = isprint(c) ? (char)c : '.';

                if (isCur) {
                    RECT r = {xAscii + j*charWidth, y,
                              xAscii + (j+1)*charWidth, y + charHeight};
                    HBRUSH cb = CreateSolidBrush(cfg.col_cursor);
                    FillRect(memDC, &r, cb);
                    DeleteObject(cb);
                    SetTextColor(memDC, RGB(0,0,0));
                } else {
                    SetTextColor(memDC, cfg.col_ascii);
                }
                TextOutA(memDC, xAscii + j*charWidth, y, &ch, 1);
            }

            y += charHeight;
        }

        /* 4. Status bar */
        RECT stR = {0, clientRect.bottom - STATUS_HEIGHT,
                    clientRect.right, clientRect.bottom};
        HBRUSH stBr = CreateSolidBrush(cfg.col_status_bg);
        FillRect(memDC, &stR, stBr);
        DeleteObject(stBr);

        SetTextColor(memDC, cfg.col_status_text);
        char statusStr[300];
        sprintf(statusStr,
            " Size: %llu | Off: %08llX | Mode: %s | Layout: %s"
            " | BPR: %d | Dirty: %llu%s",
            (unsigned long long)esize,
            (unsigned long long)editor.cursor,
            editor.edit_mode == 0 ? "HEX" : "TEXT",
            editor.view_layout == 0 ? "HEX+TXT" : "TXT ONLY",
            editor.bytes_per_row,
            (unsigned long long)editor.tracker.count,
            editor.memory_mode ? " | MEM" : "");
        TextOutA(memDC, 10, clientRect.bottom - 20,
                 statusStr, (int)strlen(statusStr));

        /* 5. RO / RW button (bottom-right) */
        RECT btnR = GetBtnRect();
        HBRUSH btnBr = CreateSolidBrush(
            editor.readonly_mode ? cfg.col_ro_btn : cfg.col_rw_btn);
        FillRect(memDC, &btnR, btnBr);
        DeleteObject(btnBr);
        SetTextColor(memDC, RGB(255,255,255));
        const char *btnTxt = editor.readonly_mode ? "RO" : "RW";
        DrawTextA(memDC, btnTxt, -1, &btnR,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        /* Blit */
        BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom,
               memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        break;
    }

    /* ---- Left mouse button ---- */
    case WM_LBUTTONDOWN: {
        int xPos = LOWORD(lParam);
        int yPos = HIWORD(lParam);
        SetFocus(hwnd);

        /* RO/RW button */
        RECT btnR = GetBtnRect();
        POINT pt = {xPos, yPos};
        if (PtInRect(&btnR, pt)) {
            editor.readonly_mode = 1 - editor.readonly_mode;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* Menu bar */
        if (yPos < MENU_HEIGHT) {
            if (menu_visible) {
                int block = xPos / (8 * charWidth);
                switch (block) {
                    case 0: DoOpen(hwnd); break;
                    case 1: DoSave(hwnd); break;
                    case 2:
                        editor.edit_mode = 1 - editor.edit_mode;
                        hex_state = 0;
                        break;
                    case 3: {
                        int bprs[] = {8, 16, 24, 32, 48};
                        int idx = 0;
                        for (int k = 0; k < 5; k++)
                            if (bprs[k] == editor.bytes_per_row) idx = k;
                        editor.bytes_per_row = bprs[(idx + 1) % 5];
                        SnapWindowSize(hwnd, visibleRows);
                        break;
                    }
                    case 4:
                        editor.view_layout = 1 - editor.view_layout;
                        break;
                    case 5:
                        DestroyWindow(hwnd);
                        break;
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

        /* Grid click */
        int mode;
        size_t off = GridHitTest(xPos, yPos, &mode);
        if (off != (size_t)-1) {
            editor.edit_mode = mode;

            if (GetKeyState(VK_SHIFT) & 0x8000) {
                /* Shift+Click: extend selection */
                if (!has_selection(&editor))
                    editor.selection_start = editor.cursor;
                editor.selection_end = off;
            } else {
                /* Normal click: begin potential drag-select */
                clear_selection(&editor);
                editor.selection_start = off;
                editor.selection_end   = off;
                is_dragging = 1;
                SetCapture(hwnd);
            }
            editor.cursor = off;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }

    case WM_LBUTTONUP:
        if (is_dragging) {
            is_dragging = 0;
            ReleaseCapture();
            /* If start == end, it was a plain click – clear selection */
            if (editor.selection_start == editor.selection_end)
                clear_selection(&editor);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;

    /* ---- Mouse wheel (fast scroll, no flicker) ---- */
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        int rows  = abs(delta) / WHEEL_DELTA;
        size_t amt = (size_t)rows * (size_t)bpr;
        size_t esize = get_effective_size(&editor);

        if (delta > 0) {
            editor.view_offset = (editor.view_offset >= amt)
                                     ? editor.view_offset - amt : 0;
        } else {
            editor.view_offset += amt;
            if (esize > 0) {
                size_t maxOff = ((esize - 1) / (size_t)bpr) * (size_t)bpr;
                if (editor.view_offset > maxOff)
                    editor.view_offset = maxOff;
            } else {
                editor.view_offset = 0;
            }
        }
        InvalidateRect(hwnd, NULL, FALSE);   /* FALSE = no erase */
        break;
    }

    /* ---- Drag & Drop ---- */
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam;
        char fp[MAX_PATH];
        DragQueryFileA(hDrop, 0, fp, MAX_PATH);
        cleanup_editor(&editor);
        editor.bytes_per_row = cfg.bytes_per_row;
        editor.view_layout   = cfg.view_layout;
        editor.edit_mode     = cfg.edit_mode;
        editor.readonly_mode = 0;
        init_file(&editor, fp);
        DragFinish(hDrop);
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    }

    /* ---- Keyboard navigation + selection ---- */
    case WM_KEYDOWN: {
        int shift = GetKeyState(VK_SHIFT) & 0x8000;
        int ctrl  = GetKeyState(VK_CONTROL) & 0x8000;
        size_t esize = get_effective_size(&editor);

        /* Ctrl+C: copy selection */
        if (ctrl && wParam == 'C') {
            CopySelectionToClipboard(hwnd);
            break;
        }
        /* Ctrl+S: save */
        if (ctrl && wParam == 'S') {
            DoSave(hwnd);
            break;
        }

        /* Begin selection on first Shift+Arrow */
        if (shift && !has_selection(&editor)) {
            editor.selection_start = editor.cursor;
            editor.selection_end   = editor.cursor;
        }

        size_t old_cursor = editor.cursor;

        switch (wParam) {
            case VK_UP:
                if (editor.cursor >= (size_t)bpr)
                    editor.cursor -= (size_t)bpr;
                break;
            case VK_DOWN:
                if (editor.cursor + (size_t)bpr < esize || editor.memory_mode)
                    editor.cursor += (size_t)bpr;
                break;
            case VK_LEFT:
                if (editor.cursor > 0) editor.cursor--;
                break;
            case VK_RIGHT:
                if (editor.cursor + 1 < esize || editor.memory_mode)
                    editor.cursor++;
                break;
            case VK_PRIOR:
                if (editor.cursor >= (size_t)(bpr * visibleRows))
                    editor.cursor -= (size_t)bpr * visibleRows;
                else
                    editor.cursor = 0;
                break;
            case VK_NEXT:
                if (editor.cursor + (size_t)(bpr * visibleRows) < esize)
                    editor.cursor += (size_t)bpr * visibleRows;
                else if (esize > 0)
                    editor.cursor = esize - 1;
                break;
            case VK_HOME:
                editor.cursor = 0;
                break;
            case VK_END:
                if (esize > 0) editor.cursor = esize - 1;
                break;
            case VK_F2:
                DoSave(hwnd);
                break;
            case VK_F4:
                editor.view_layout = 1 - editor.view_layout;
                break;
            default:
                break;
        }

        /* Update selection_end if Shift held */
        if (shift && has_selection(&editor)) {
            editor.selection_end = editor.cursor;
        } else if (!shift && editor.cursor != old_cursor) {
            clear_selection(&editor);
        }

        EnsureCursorVisible();
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    }

    /* ---- Character input (hex / text editing) ---- */
    case WM_CHAR: {
        size_t esize = get_effective_size(&editor);

        if (editor.readonly_mode) break;

        if (editor.edit_mode == 0) {
            /* Hex input */
            int val = -1;
            char c = (char)wParam;
            if      (c >= '0' && c <= '9') val = c - '0';
            else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;

            if (val != -1) {
                if (hex_state == 0) {
                    temp_hex  = (uint8_t)(val << 4);
                    hex_state = 1;
                } else {
                    uint8_t byte = temp_hex | (uint8_t)val;
                    set_byte(&editor, editor.cursor, byte);
                    hex_state = 0;
                    if (editor.cursor + 1 < esize || editor.memory_mode)
                        editor.cursor++;
                }
                EnsureCursorVisible();
                InvalidateRect(hwnd, NULL, FALSE);
            }
        } else {
            /* Text input */
            if (isprint((int)wParam)) {
                set_byte(&editor, editor.cursor, (uint8_t)wParam);
                if (editor.cursor + 1 < esize || editor.memory_mode)
                    editor.cursor++;
                EnsureCursorVisible();
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        break;
    }

    /* ---- Cleanup ---- */
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

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd;

    load_config();

    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;   /* We handle all painting */
    wc.lpszClassName = "HexEditorClass";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_ACCEPTFILES | WS_EX_COMPOSITED,
        "HexEditorClass", "Hex Editor",
        WS_POPUP | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
        NULL, NULL, hInst, NULL);

    /* Initialise editor */
    memset(&editor, 0, sizeof(editor));
    editor.bytes_per_row = cfg.bytes_per_row;
    editor.view_layout   = cfg.view_layout;
    editor.edit_mode     = cfg.edit_mode;
    editor.readonly_mode = 0;

    if (__argc > 1) {
        init_file(&editor, __argv[1]);
    } else {
        init_memory_mode(&editor);
        editor.bytes_per_row = cfg.bytes_per_row;
        editor.view_layout   = cfg.view_layout;
        editor.edit_mode     = cfg.edit_mode;
    }

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}