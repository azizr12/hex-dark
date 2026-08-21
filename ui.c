#include "app.h"
#include "ui.h"
#include "fileio.h"

#define IDC_VSCROLLBAR  1001
#define MENU_HEIGHT     35
#define STATUS_HEIGHT   28
#define BTN_W           64
#define BTN_H           20
#define BORDER_PX       5

static BOOL menu_visible = TRUE;
static int  is_dragging  = 0;

static int VScrollWidth(void) { return GetSystemMetrics(SM_CXVSCROLL); }

static void LayoutScrollBar(HWND hwnd) {
    if (!g_hScroll) return;
    int w = VScrollWidth();
    int x = fullClientRect.right - w;
    int y = MENU_HEIGHT;
    int h = fullClientRect.bottom - MENU_HEIGHT - STATUS_HEIGHT;
    if (x < 0) x = 0;
    if (h < 0) h = 0;
    MoveWindow(g_hScroll, x, y, w, h, TRUE);
}

void UpdateVScroll(HWND hwnd) {
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
    if (si.nMax == 0 || (int)si.nPage > si.nMax) EnableScrollBar(g_hScroll, SB_CTL, ESB_DISABLE_BOTH);
    else EnableScrollBar(g_hScroll, SB_CTL, ESB_ENABLE_BOTH);
}

static void SetViewRow(HWND hwnd, size_t row) {
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

static void HandleVScroll(HWND hwnd, WPARAM wParam, LPARAM lParam) {
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
        default: SetFocus(hwnd); return;
    }

    if (pos < 0) pos = 0;
    if (pos > bottom) pos = bottom;
    if (pos != oldPos) SetViewRow(hwnd, (size_t)pos);
    SetFocus(hwnd);
}

static void WheelScroll(HWND hwnd, int delta) {
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

void OnSize(HWND hwnd, int cx, int cy) {
    fullClientRect.left = 0; fullClientRect.top = 0;
    fullClientRect.right = cx; fullClientRect.bottom = cy;
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

void CreateVScrollBar(HWND hwnd) {
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

void SnapWindowSize(HWND hwnd, int rows) {
    if (charHeight == 0) return;
    if (rows < 1) rows = 1;
    RECT wr, cr;
    GetWindowRect(hwnd, &wr); GetClientRect(hwnd, &cr);
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

void ClampBPRForLayout(HWND hwnd) {
    int limit = (editor.view_layout == 1) ? (48 * 4) : 48;
    if (editor.bytes_per_row > limit) {
        editor.bytes_per_row = limit;
        SnapWindowSize(hwnd, visibleRows);
    }
    UpdateVScroll(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

void CycleBPR(HWND hwnd) {
    static const int values[] = { 8, 16, 24, 32, 48, 64, 96, 128, 192 };
    int limit = (editor.view_layout == 1) ? (48 * 4) : 48;
    int current = editor.bytes_per_row;
    int next = 8;
    if (current < limit) {
        next = limit;
        for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
            if (values[i] > current && values[i] <= limit) { next = values[i]; break; }
        }
    }
    editor.bytes_per_row = next;
    SnapWindowSize(hwnd, visibleRows);
    UpdateVScroll(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

void EnsureCursorVisible(void) {
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    size_t cursor_row = editor.cursor / (size_t)bpr;
    size_t view_row = editor.view_offset / (size_t)bpr;
    if (cursor_row < view_row) editor.view_offset = cursor_row * (size_t)bpr;
    else if (cursor_row >= view_row + (size_t)visibleRows) editor.view_offset = (cursor_row - (size_t)visibleRows + 1) * (size_t)bpr;
}

static RECT GetBtnRect(void) {
    RECT r;
    r.right = clientRect.right - 8; r.left = r.right - BTN_W;
    r.bottom = clientRect.bottom - 4; r.top = r.bottom - BTN_H;
    if (r.left < 0) { r.left = 0; r.right = BTN_W; }
    if (r.top < 0) { r.top = 0; r.bottom = BTN_H; }
    return r;
}

static size_t GridHitTest(int xPos, int yPos, int *out_mode) {
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    int xHex = 10 * charWidth;
    int xAscii = (editor.view_layout == 0) ? xHex + (bpr * 3 + 2) * charWidth : xHex;
    if (yPos < MENU_HEIGHT || yPos >= clientRect.bottom - STATUS_HEIGHT) return (size_t)-1;

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
        if (off < get_virtual_size()) return off;
    }
    return (size_t)-1;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
        RECT wr, cr; GetWindowRect(hwnd, &wr); GetClientRect(hwnd, &cr);
        int fh = (wr.bottom - wr.top) - (cr.bottom - cr.top);
        int avail = (r->bottom - r->top) - fh - MENU_HEIGHT - STATUS_HEIGHT;
        int rows = avail / charHeight;
        if (rows < 1) rows = 1;
        int snapped = MENU_HEIGHT + STATUS_HEIGHT + rows * charHeight + fh;
        switch (wParam) {
            case WMSZ_TOP: case WMSZ_TOPLEFT: case WMSZ_TOPRIGHT: r->top = r->bottom - snapped; return TRUE;
            case WMSZ_BOTTOM: case WMSZ_BOTTOMLEFT: case WMSZ_BOTTOMRIGHT: r->bottom = r->top + snapped; return TRUE;
            default: return TRUE;
        }
    }
    case WM_CREATE:
        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, 1, (UINT)cfg.menu_hide_delay, NULL);
        hFont = CreateFontA(cfg.font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, cfg.font_name);
        if (!hFont) hFont = (HFONT)GetStockObject(ANSI_FIXED_FONT);
        {
            HDC hdc = GetDC(hwnd);
            SelectObject(hdc, hFont);
            TEXTMETRIC tm; GetTextMetrics(hdc, &tm);
            charWidth = tm.tmAveCharWidth;
            charHeight = tm.tmHeight + tm.tmExternalLeading;
            ReleaseDC(hwnd, hdc);
        }
        SnapWindowSize(hwnd, cfg.rows);
        CreateVScrollBar(hwnd);
        { RECT rcNow; if (GetClientRect(hwnd, &rcNow)) OnSize(hwnd, rcNow.right, rcNow.bottom); }
        ClampBPRForLayout(hwnd);
        SetFocus(hwnd);
        break;
    case WM_SIZE: OnSize(hwnd, LOWORD(lParam), HIWORD(lParam)); break;
    case WM_VSCROLL: HandleVScroll(hwnd, wParam, lParam); break;
    case WM_MOUSEMOVE: {
        POINTS pts = MAKEPOINTS(lParam);
        if (pts.y < MENU_HEIGHT) {
            if (!menu_visible) { menu_visible = TRUE; InvalidateRect(hwnd, NULL, FALSE); }
            SetTimer(hwnd, 1, (UINT)cfg.menu_hide_delay, NULL);
        }
        if (is_dragging && (wParam & MK_LBUTTON)) {
            int m; size_t off = GridHitTest(pts.x, pts.y, &m);
            if (off != (size_t)-1) { editor.selection_end = off; editor.cursor = off; hex_state = 0; InvalidateRect(hwnd, NULL, FALSE); }
        }
        break;
    }
    case WM_TIMER:
        if (wParam == 1 && menu_visible) { menu_visible = FALSE; InvalidateRect(hwnd, NULL, FALSE); }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT paintRect = fullClientRect;
        if (paintRect.right <= 0 || paintRect.bottom <= 0) paintRect = clientRect;
        if (paintRect.right <= 0 || paintRect.bottom <= 0) { EndPaint(hwnd, &ps); return 0; }

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, paintRect.right, paintRect.bottom);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
        HBRUSH bgBr = CreateSolidBrush(cfg.col_background);
        FillRect(memDC, &paintRect, bgBr); DeleteObject(bgBr);
        SelectObject(memDC, hFont); SetBkMode(memDC, TRANSPARENT);

        RECT topR = {0, 0, paintRect.right, MENU_HEIGHT};
        HBRUSH menuBr = CreateSolidBrush(cfg.col_menu_bg);
        FillRect(memDC, &topR, menuBr); DeleteObject(menuBr);

        if (menu_visible) {
            SetTextColor(memDC, cfg.col_menu_text);
            const char *menuTxt = "[O]pen  [S]ave  [M]ode  [B]PR  [V]iew  [X]xit";
            TextOutA(memDC, 15, 8, menuTxt, (int)strlen(menuTxt));
        } else {
            SetTextColor(memDC, cfg.col_menu_hidden);
            const char *fname = editor.filename;
            const char *p1 = strrchr(fname, '\\'); const char *p2 = strrchr(fname, '/');
            if (p1 || p2) fname = (p1 > p2) ? p1 + 1 : p2 + 1;
            char title[512]; snprintf(title, sizeof(title), "RAM-Only Hex Editor [%s]", fname);
            TextOutA(memDC, 15, 8, title, (int)strlen(title));
        }

        int y = MENU_HEIGHT; int xHex = 10 * charWidth;
        size_t virtual_size = get_virtual_size();
        int selActive = has_selection(&editor);
        size_t sLo = selActive ? sel_min(&editor) : 0;
        size_t sHi = selActive ? sel_max(&editor) : 0;

        for (int i = 0; i < visibleRows; i++) {
            size_t offset = editor.view_offset + (size_t)i * (size_t)bpr;
            if (offset >= virtual_size) break;
            SetTextColor(memDC, cfg.col_offset);
            char offStr[16]; sprintf(offStr, "%08llX  ", (unsigned long long)offset);
            TextOutA(memDC, 0, y, offStr, (int)strlen(offStr));

            int xAscii;
            if (editor.view_layout == 0) {
                xAscii = xHex + (bpr * 3 + 2) * charWidth;
                for (int j = 0; j < bpr; j++) {
                    size_t off = offset + (size_t)j;
                    if (off >= virtual_size) break;
                    int isSel = selActive && off >= sLo && off <= sHi;
                    int isCur = (off == editor.cursor);
                    if (isSel) { RECT r = { xHex + j * 3 * charWidth, y, xHex + (j + 1) * 3 * charWidth, y + charHeight }; HBRUSH sb = CreateSolidBrush(cfg.col_selection); FillRect(memDC, &r, sb); DeleteObject(sb); }
                    char hexStr[4]; sprintf(hexStr, "%02X ", get_byte(&editor, off));
                    if (isCur) { RECT r = { xHex + j * 3 * charWidth, y, xHex + (j + 1) * 3 * charWidth, y + charHeight }; HBRUSH cb = CreateSolidBrush(cfg.col_cursor); FillRect(memDC, &r, cb); DeleteObject(cb); SetTextColor(memDC, RGB(0, 0, 0)); } 
                    else { SetTextColor(memDC, cfg.col_hex); }
                    TextOutA(memDC, xHex + j * 3 * charWidth, y, hexStr, 3);
                }
            } else { xAscii = xHex; }

            for (int j = 0; j < bpr; j++) {
                size_t off = offset + (size_t)j;
                if (off >= virtual_size) break;
                int isSel = selActive && off >= sLo && off <= sHi;
                int isCur = (off == editor.cursor);
                if (isSel) { RECT r = { xAscii + j * charWidth, y, xAscii + (j + 1) * charWidth, y + charHeight }; HBRUSH sb = CreateSolidBrush(cfg.col_selection); FillRect(memDC, &r, sb); DeleteObject(sb); }
                uint8_t c = get_byte(&editor, off); char ch = isprint(c) ? (char)c : '.';
                if (isCur) { RECT r = { xAscii + j * charWidth, y, xAscii + (j + 1) * charWidth, y + charHeight }; HBRUSH cb = CreateSolidBrush(cfg.col_cursor); FillRect(memDC, &r, cb); DeleteObject(cb); SetTextColor(memDC, RGB(0, 0, 0)); } 
                else { SetTextColor(memDC, cfg.col_ascii); }
                TextOutA(memDC, xAscii + j * charWidth, y, &ch, 1);
            }
            y += charHeight;
        }

        RECT stR = { 0, paintRect.bottom - STATUS_HEIGHT, paintRect.right, paintRect.bottom };
        HBRUSH stBr = CreateSolidBrush(cfg.col_status_bg); FillRect(memDC, &stR, stBr); DeleteObject(stBr);
        SetTextColor(memDC, cfg.col_status_text);
        char statusStr[300];
        sprintf(statusStr, " Size: %llu | Off: %08llX | Mode: %s | Layout: %s | BPR: %d | Dirty: %llu%s",
            (unsigned long long)get_effective_size(&editor), (unsigned long long)editor.cursor,
            editor.edit_mode == 0 ? "HEX" : "TEXT", editor.view_layout == 0 ? "HEX+TXT" : "TXT ONLY",
            editor.bytes_per_row, (unsigned long long)editor.tracker.count, editor.memory_mode ? " | MEM" : "");
        TextOutA(memDC, 10, paintRect.bottom - 20, statusStr, (int)strlen(statusStr));

        RECT btnR = GetBtnRect();
        HBRUSH btnBr = CreateSolidBrush(editor.readonly_mode ? cfg.col_ro_btn : cfg.col_rw_btn);
        FillRect(memDC, &btnR, btnBr); DeleteObject(btnBr);
        SetTextColor(memDC, RGB(255, 255, 255));
        const char *btnTxt = editor.readonly_mode ? "RO" : "RW";
        DrawTextA(memDC, btnTxt, -1, &btnR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        BitBlt(hdc, 0, 0, paintRect.right, paintRect.bottom, memDC, 0, 0, SRCCOPY);
        SelectObject(memDC, oldBmp); DeleteObject(memBmp); DeleteDC(memDC);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_LBUTTONDOWN: {
        int xPos = LOWORD(lParam); int yPos = HIWORD(lParam);
        SetFocus(hwnd);
        RECT btnR = GetBtnRect(); POINT pt = {xPos, yPos};
        if (PtInRect(&btnR, pt)) {
            editor.readonly_mode = 1 - editor.readonly_mode; hex_state = 0;
            if (editor.readonly_mode) {
                size_t s = get_effective_size(&editor);
                if (s == 0) editor.cursor = 0;
                else if (editor.cursor >= s) editor.cursor = s - 1;
                clear_selection(&editor); EnsureCursorVisible();
            }
            UpdateVScroll(hwnd); InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        if (yPos < MENU_HEIGHT) {
            if (menu_visible) {
                int block = xPos / (8 * charWidth);
                switch (block) {
                    case 0: DoOpen(hwnd); break;
                    case 1: DoSave(hwnd); break;
                    case 2: editor.edit_mode = 1 - editor.edit_mode; hex_state = 0; break;
                    case 3: CycleBPR(hwnd); break;
                    case 4: editor.view_layout = 1 - editor.view_layout; ClampBPRForLayout(hwnd); break;
                    case 5: DestroyWindow(hwnd); break;
                    default: ReleaseCapture(); SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); break;
                }
            } else { ReleaseCapture(); SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
            InvalidateRect(hwnd, NULL, FALSE); return 0;
        }
        int mode; size_t off = GridHitTest(xPos, yPos, &mode);
        if (off != (size_t)-1) {
            editor.edit_mode = mode; hex_state = 0;
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                if (editor.selection_start == (size_t)-1) editor.selection_start = editor.cursor;
                editor.selection_end = off;
            } else {
                clear_selection(&editor);
                editor.selection_start = off; editor.selection_end = off;
                is_dragging = 1; SetCapture(hwnd);
            }
            editor.cursor = off; EnsureCursorVisible(); UpdateVScroll(hwnd); InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }
    case WM_LBUTTONUP:
        if (is_dragging) {
            is_dragging = 0; ReleaseCapture();
            if (editor.selection_start == editor.selection_end) clear_selection(&editor);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case WM_MOUSEWHEEL: WheelScroll(hwnd, GET_WHEEL_DELTA_WPARAM(wParam)); break;
    case WM_DROPFILES: {
        HDROP hDrop = (HDROP)wParam; char fp[MAX_PATH];
        DragQueryFileA(hDrop, 0, fp, MAX_PATH);
        cleanup_editor(&editor);
        if (init_file(&editor, fp) != 0) init_memory_mode(&editor);
        editor.bytes_per_row = cfg.bytes_per_row; editor.view_layout = cfg.view_layout;
        editor.edit_mode = cfg.edit_mode; editor.readonly_mode = 0;
        editor.cursor = 0; editor.view_offset = 0; clear_selection(&editor); hex_state = 0;
        DragFinish(hDrop); ClampBPRForLayout(hwnd);
        break;
    }
    case WM_KEYDOWN: {
        int shift = GetKeyState(VK_SHIFT) & 0x8000;
        int ctrl  = GetKeyState(VK_CONTROL) & 0x8000;
        if (ctrl && wParam == 'C') { CopySelectionToClipboard(hwnd); break; }
        if (ctrl && wParam == 'S') { DoSave(hwnd); break; }
        if (shift && editor.selection_start == (size_t)-1) { editor.selection_start = editor.cursor; editor.selection_end = editor.cursor; }

        size_t old_cursor = editor.cursor;
        size_t vs = get_virtual_size();
        int can_extend = !editor.readonly_mode;
        size_t page = (size_t)bpr * (size_t)visibleRows;

        switch (wParam) {
            case VK_UP: if (editor.cursor >= (size_t)bpr) editor.cursor -= (size_t)bpr; break;
            case VK_DOWN:
                if (can_extend && editor.memory_mode) editor.cursor += (size_t)bpr;
                else if (editor.cursor + (size_t)bpr < vs) editor.cursor += (size_t)bpr;
                break;
            case VK_LEFT: if (editor.cursor > 0) editor.cursor--; break;
            case VK_RIGHT:
                if (can_extend && editor.memory_mode) editor.cursor++;
                else if (vs > 0 && editor.cursor < vs - 1) editor.cursor++;
                break;
            case VK_PRIOR: if (editor.cursor >= page) editor.cursor -= page; else editor.cursor = 0; break;
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
        EnsureCursorVisible(); UpdateVScroll(hwnd); InvalidateRect(hwnd, NULL, FALSE);
        break;
    }
    case WM_CHAR: {
        if (editor.readonly_mode) break;
        if (editor.edit_mode == 0) {
            int val = -1; char c = (char)wParam;
            if (c >= '0' && c <= '9') val = c - '0';
            else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
            if (val != -1) {
                if (hex_state == 0) { temp_hex = (uint8_t)(val << 4); hex_state = 1; }
                else {
                    uint8_t byte = temp_hex | (uint8_t)val;
                    set_byte(&editor, editor.cursor, byte); hex_state = 0;
                    size_t new_size = get_effective_size(&editor);
                    if (editor.memory_mode) editor.cursor++;
                    else if (editor.cursor < new_size) editor.cursor++;
                    EnsureCursorVisible(); UpdateVScroll(hwnd); InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        } else {
            if (isprint((int)wParam)) {
                set_byte(&editor, editor.cursor, (uint8_t)wParam);
                size_t new_size = get_effective_size(&editor);
                if (editor.memory_mode) editor.cursor++;
                else if (editor.cursor < new_size) editor.cursor++;
                EnsureCursorVisible(); UpdateVScroll(hwnd); InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        break;
    }
    case WM_DESTROY:
        KillTimer(hwnd, 1); cleanup_editor(&editor);
        if (hFont) DeleteObject(hFont);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}