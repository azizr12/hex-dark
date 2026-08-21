#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ========================================================================
 * 1. Core Data Structures & File I/O (RAM-Only Engine)
 * ======================================================================== */

#define WINDOW_SIZE 4096

typedef struct { size_t offset; uint8_t original; uint8_t modified; } DirtyByte;
typedef struct { DirtyByte *items; size_t count; size_t capacity; } DirtyTracker;

typedef struct {
    FILE *fp; 
    size_t file_size; 
    size_t cursor; 
    size_t view_offset;
    size_t selection_start; 
    size_t selection_end;
    int bytes_per_row; 
    int edit_mode;       // 0=Hex, 1=ASCII
    int view_layout;     // 0=Hex+ASCII, 1=ASCII Only
    uint8_t window[WINDOW_SIZE]; 
    size_t window_start; 
    size_t window_len;
    char filename[256]; 
    DirtyTracker tracker;
} HexEditor;

void init_tracker(DirtyTracker *t) { 
    t->capacity = 1024; t->count = 0; 
    t->items = (DirtyByte *)malloc(t->capacity * sizeof(DirtyByte)); 
}

void load_window(HexEditor *ed, size_t target_offset) {
    int bpr = ed->bytes_per_row; 
    ed->window_start = (target_offset / bpr) * bpr;
    fseek(ed->fp, (long)ed->window_start, SEEK_SET);
    ed->window_len = fread(ed->window, 1, WINDOW_SIZE, ed->fp);
}

uint8_t get_byte(HexEditor *ed, size_t offset) {
    if (offset >= ed->file_size) return 0;
    for (size_t i = 0; i < ed->tracker.count; i++) 
        if (ed->tracker.items[i].offset == offset) return ed->tracker.items[i].modified;
    
    if (offset < ed->window_start || offset >= ed->window_start + ed->window_len) 
        load_window(ed, offset);
    
    return ed->window[offset - ed->window_start];
}

void set_byte(HexEditor *ed, size_t offset, uint8_t value) {
    if (offset >= ed->file_size) return;
    for (size_t i = 0; i < ed->tracker.count; i++) {
        if (ed->tracker.items[i].offset == offset) { 
            ed->tracker.items[i].modified = value; 
            return; 
        }
    }
    if (offset < ed->window_start || offset >= ed->window_start + ed->window_len) 
        load_window(ed, offset);
    
    uint8_t true_orig = ed->window[offset - ed->window_start];
    if (ed->tracker.count == ed->tracker.capacity) { 
        ed->tracker.capacity *= 2; 
        ed->tracker.items = (DirtyByte *)realloc(ed->tracker.items, ed->tracker.capacity * sizeof(DirtyByte)); 
    }
    ed->tracker.items[ed->tracker.count].offset = offset; 
    ed->tracker.items[ed->tracker.count].original = true_orig; 
    ed->tracker.items[ed->tracker.count].modified = value; 
    ed->tracker.count++;
}

int save_dirty(DirtyTracker *t, FILE *fp) {
    for (size_t i = 0; i < t->count; i++) { 
        fseek(fp, (long)t->items[i].offset, SEEK_SET); 
        if (fwrite(&t->items[i].modified, 1, 1, fp) != 1) return -1; 
    }
    fflush(fp); t->count = 0; return 0;
}

int init_file(HexEditor *ed, const char *filename) {
    ed->fp = fopen(filename, "r+b"); 
    if (!ed->fp) { 
        ed->fp = fopen(filename, "w+b"); 
        if (!ed->fp) return -1; 
    }
    fseek(ed->fp, 0, SEEK_END); 
    ed->file_size = (size_t)ftell(ed->fp); 
    fseek(ed->fp, 0, SEEK_SET);
    strncpy(ed->filename, filename, sizeof(ed->filename) - 1); 
    init_tracker(&ed->tracker); 
    ed->cursor = 0; ed->view_offset = 0; 
    ed->selection_start = 0; ed->selection_end = 0;
    return 0;
}

void cleanup_editor(HexEditor *ed) { 
    if (ed->fp) fclose(ed->fp); 
    free(ed->tracker.items); 
    memset(ed, 0, sizeof(HexEditor));
}

/* ========================================================================
 * 2. GUI Configuration & Global State
 * ======================================================================== */

#define MENU_HEIGHT 35
#define STATUS_HEIGHT 25
#define MENU_HIDE_DELAY 2000

HexEditor editor = {0};
static int hex_state = 0; 
static uint8_t temp_hex = 0;
HFONT hFont; 
int charWidth, charHeight; 
int visibleRows; 
RECT clientRect;
BOOL menu_visible = TRUE;

/* ========================================================================
 * 3. Helper Functions (Snapping & Clipboard)
 * ======================================================================== */

void SnapWindowSize(HWND hwnd, int rows) {
    if (charHeight == 0) return;
    RECT win_rect, client_rect;
    GetWindowRect(hwnd, &win_rect);
    GetClientRect(hwnd, &client_rect);
    
    int frame_height = (win_rect.bottom - win_rect.top) - (client_rect.bottom - client_rect.top);
    int frame_width = (win_rect.right - win_rect.left) - (client_rect.right - client_rect.left);
    
    int target_client_height = MENU_HEIGHT + STATUS_HEIGHT + (rows * charHeight);
    int target_win_height = target_client_height + frame_height;
    
    SetWindowPos(hwnd, NULL, win_rect.left, win_rect.top, 
                 (client_rect.right - client_rect.left) + frame_width, 
                 target_win_height, SWP_NOZORDER);
}

void CopySelectionToClipboard(HexEditor *ed) {
    size_t start = ed->selection_start < ed->selection_end ? ed->selection_start : ed->selection_end;
    size_t end = ed->selection_start < ed->selection_end ? ed->selection_end : ed->selection_start;
    
    if (start == end || start >= ed->file_size) return;
    if (end > ed->file_size) end = ed->file_size;

    int bpr = ed->bytes_per_row;
    size_t max_rows = (end - start + bpr - 1) / bpr;
    size_t buf_size = max_rows * (8 + 2 + (bpr * 2) + 2 + bpr + 2) + 1; // Offset + Hex + Ascii + \r\n
    char *clip_buf = (char *)malloc(buf_size);
    if (!clip_buf) return;
    clip_buf[0] = '\0';

    size_t current = start;
    char *ptr = clip_buf;
    
    while (current < end) {
        size_t row_start = (current / bpr) * bpr;
        size_t row_end = row_start + bpr;
        if (row_end > ed->file_size) row_end = ed->file_size;

        // 1. Offset
        ptr += sprintf(ptr, "%08llX  ", (unsigned long long)row_start);

        // 2. Hex (Continuous, no spaces, padded for unselected bytes in row)
        for (size_t j = row_start; j < row_end; j++) {
            if (j >= start && j < end) {
                ptr += sprintf(ptr, "%02X", get_byte(ed, j));
            } else {
                ptr += sprintf(ptr, "  "); 
            }
        }
        ptr += sprintf(ptr, "  ");

        // 3. ASCII (Continuous as-is, padded for unselected bytes)
        for (size_t j = row_start; j < row_end; j++) {
            if (j >= start && j < end) {
                uint8_t c = get_byte(ed, j);
                *ptr++ = isprint(c) ? (char)c : '.';
            } else {
                *ptr++ = ' ';
            }
        }
        ptr += sprintf(ptr, "\r\n");
        current = row_end;
    }

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hglbCopy = GlobalAlloc(GMEM_MOVEABLE, (ptr - clip_buf) + 1);
        if (hglbCopy) {
            char *lptstrCopy = (char *)GlobalLock(hglbCopy);
            memcpy(lptstrCopy, clip_buf, (ptr - clip_buf) + 1);
            GlobalUnlock(hglbCopy);
            SetClipboardData(CF_TEXT, hglbCopy);
        }
        CloseClipboard();
    }
    free(clip_buf);
}

/* ========================================================================
 * 4. Rendering Engine
 * ======================================================================== */

void RenderFrame(HWND hwnd, HDC hdc) {
    int bpr = editor.bytes_per_row;
    HDC memDC = CreateCompatibleDC(hdc); 
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // Background
    HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 30));
    FillRect(memDC, &clientRect, bgBrush); 
    DeleteObject(bgBrush);
    SelectObject(memDC, hFont); 
    SetBkMode(memDC, TRANSPARENT);

    // Top Menu / Title Bar
    RECT topRect = {0, 0, clientRect.right, MENU_HEIGHT};
    FillRect(memDC, &topRect, CreateSolidBrush(RGB(45, 45, 45)));
    
    if (menu_visible) {
        SetTextColor(memDC, RGB(220, 220, 220));
        TextOut(memDC, 15, 8, "[O]pen  [S]ave  [M]ode  [B]PR  [V]iew  [X]xit", 44);
    } else {
        SetTextColor(memDC, RGB(100, 100, 100));
        const char *fname = editor.filename;
        const char *p1 = strrchr(fname, '\\');
        const char *p2 = strrchr(fname, '/');
        if (p1 || p2) fname = (p1 > p2) ? p1 + 1 : p2 + 1;
        
        char titleStr[512];
        snprintf(titleStr, sizeof(titleStr), "RAM-Only Hex Editor [%s]", fname);
        TextOut(memDC, 15, 8, titleStr, (int)strlen(titleStr));
    }

    // Grid Rendering
    int y = MENU_HEIGHT;
    int xHex = 10 * charWidth;
    int xAscii = (editor.view_layout == 0) ? (xHex + (bpr * 3 + 2) * charWidth) : xHex;
    
    size_t sel_start = editor.selection_start < editor.selection_end ? editor.selection_start : editor.selection_end;
    size_t sel_end = editor.selection_start < editor.selection_end ? editor.selection_end : editor.selection_start;

    for (int i = 0; i < visibleRows; i++) {
        size_t offset = editor.view_offset + (i * bpr);
        if (offset >= editor.file_size && editor.file_size > 0) break;

        // Offset (Cyan)
        SetTextColor(memDC, RGB(0, 255, 255)); 
        char offStr[16]; 
        sprintf(offStr, "%08llX  ", (unsigned long long)offset);
        TextOut(memDC, 0, y, offStr, (int)strlen(offStr));

        if (editor.view_layout == 0) {
            SetTextColor(memDC, RGB(0, 255, 0));
            for (int j = 0; j < bpr; j++) {
                if (offset + j < editor.file_size) {
                    char hexStr[4]; 
                    sprintf(hexStr, "%02X ", get_byte(&editor, offset + j));
                    
                    BOOL is_sel = (offset + j >= sel_start && offset + j < sel_end);
                    BOOL is_cursor = (offset + j == editor.cursor && editor.edit_mode == 0);

                    if (is_sel) {
                        RECT r = {xHex + j*3*charWidth, y, xHex + (j+1)*3*charWidth, y + charHeight};
                        FillRect(memDC, &r, CreateSolidBrush(RGB(0, 100, 200))); 
                    }
                    if (is_cursor) {
                        RECT r = {xHex + j*3*charWidth, y, xHex + (j+1)*3*charWidth, y + charHeight};
                        FillRect(memDC, &r, CreateSolidBrush(RGB(0, 150, 255))); 
                        SetTextColor(memDC, RGB(0, 0, 0));
                    }
                    
                    TextOut(memDC, xHex + j*3*charWidth, y, hexStr, 3);
                    if (is_cursor) SetTextColor(memDC, RGB(0, 255, 0));
                }
            }
        }

        // ASCII (Yellow)
        SetTextColor(memDC, RGB(255, 255, 0));
        for (int j = 0; j < bpr; j++) {
            if (offset + j < editor.file_size) {
                uint8_t c = get_byte(&editor, offset + j); 
                char ch = isprint(c) ? (char)c : '.';
                
                BOOL is_sel = (offset + j >= sel_start && offset + j < sel_end);
                BOOL is_cursor = (offset + j == editor.cursor && editor.edit_mode == 1);

                if (is_sel) {
                    RECT r = {xAscii + j*charWidth, y, xAscii + (j+1)*charWidth, y + charHeight};
                    FillRect(memDC, &r, CreateSolidBrush(RGB(0, 100, 200))); 
                }
                if (is_cursor) {
                    RECT r = {xAscii + j*charWidth, y, xAscii + (j+1)*charWidth, y + charHeight};
                    FillRect(memDC, &r, CreateSolidBrush(RGB(0, 150, 255))); 
                    SetTextColor(memDC, RGB(0, 0, 0));
                }

                TextOut(memDC, xAscii + j*charWidth, y, &ch, 1);
                if (is_cursor) SetTextColor(memDC, RGB(255, 255, 0));
            }
        }
        y += charHeight;
    }

    // Status Bar
    RECT statusRect = {0, clientRect.bottom - STATUS_HEIGHT, clientRect.right, clientRect.bottom};
    FillRect(memDC, &statusRect, CreateSolidBrush(RGB(40, 40, 40)));
    SetTextColor(memDC, RGB(180, 180, 180)); 
    char statusStr[256];
    sprintf(statusStr, " Size: %llu | Off: %08llX | Mode: %s | Layout: %s | BPR: %d | Dirty: %llu", 
        (unsigned long long)editor.file_size, (unsigned long long)editor.cursor,
        editor.edit_mode == 0 ? "HEX" : "TEXT", 
        editor.view_layout == 0 ? "HEX+TXT" : "TXT ONLY",
        editor.bytes_per_row, (unsigned long long)editor.tracker.count);
    TextOut(memDC, 10, clientRect.bottom - 18, statusStr, (int)strlen(statusStr));

    BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp); 
    DeleteObject(memBmp); 
    DeleteDC(memDC); 
}

/* ========================================================================
 * 5. Input Handling
 * ======================================================================== */

void HandleKeyboard(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    int bpr = editor.bytes_per_row;
    BOOL shift = GetKeyState(VK_SHIFT) & 0x8000;
    BOOL ctrl = GetKeyState(VK_CONTROL) & 0x8000;

    switch (wParam) {
        case 'C':
            if (ctrl) { CopySelectionToClipboard(&editor); return; }
            break;
        case VK_UP: 
            if (editor.cursor >= (size_t)bpr) editor.cursor -= bpr; 
            break;
        case VK_DOWN: 
            if (editor.cursor + bpr < editor.file_size) editor.cursor += bpr; 
            break;
        case VK_LEFT: 
            if (editor.cursor > 0) editor.cursor--; 
            break;
        case VK_RIGHT: 
            if (editor.cursor + 1 < editor.file_size) editor.cursor++; 
            break;
        case VK_PRIOR: 
            if (editor.cursor >= (size_t)(bpr * visibleRows)) editor.cursor -= bpr * visibleRows; 
            else editor.cursor = 0; 
            break;
        case VK_NEXT: 
            if (editor.cursor + bpr * visibleRows < editor.file_size) editor.cursor += bpr * visibleRows; 
            else editor.cursor = editor.file_size - 1; 
            break;
        case VK_F2: 
            save_dirty(&editor.tracker, editor.fp); 
            break;
        case VK_F4: 
            editor.view_layout = 1 - editor.view_layout; 
            break;
    }

    // Update selection based on Shift key state
    if (shift) {
        editor.selection_end = editor.cursor;
    } else {
        editor.selection_start = editor.cursor;
        editor.selection_end = editor.cursor;
    }

    // Auto-scroll view to keep cursor visible
    size_t cursor_row = editor.cursor / bpr; 
    size_t view_start_row = editor.view_offset / bpr;
    if (cursor_row < view_start_row) {
        editor.view_offset = cursor_row * bpr;
    } else if (cursor_row >= view_start_row + (size_t)visibleRows) {
        editor.view_offset = (cursor_row - visibleRows + 1) * bpr;
    }
    
    InvalidateRect(hwnd, NULL, TRUE); 
}

void HandleCharInput(HWND hwnd, WPARAM wParam) {
    if (editor.file_size == 0) return;

    // Typing clears the selection and overwrites at cursor
    editor.selection_start = editor.cursor;
    editor.selection_end = editor.cursor;

    if (editor.edit_mode == 0) {
        int val = -1; 
        char c = (char)wParam;
        if (c >= '0' && c <= '9') val = c - '0'; 
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10; 
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        
        if (val != -1) {
            if (hex_state == 0) { 
                temp_hex = (uint8_t)(val << 4); 
                hex_state = 1; 
            } else { 
                set_byte(&editor, editor.cursor, temp_hex | (uint8_t)val); 
                hex_state = 0; 
                if (editor.cursor + 1 < editor.file_size) editor.cursor++; 
            }
        }
    } else { 
        if (isprint(wParam)) { 
            set_byte(&editor, editor.cursor, (uint8_t)wParam); 
            if (editor.cursor + 1 < editor.file_size) editor.cursor++; 
        } 
    }
    InvalidateRect(hwnd, NULL, TRUE);
}

void HandleMouse(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    int xPos = LOWORD(lParam); 
    int yPos = HIWORD(lParam);
    int bpr = editor.bytes_per_row;

    // Top Menu Interaction
    if (yPos < MENU_HEIGHT) {
        if (menu_visible) {
            int block = xPos / (8 * charWidth); 
            switch (block) {
                case 0: { // Open
                    OPENFILENAME ofn = {0}; 
                    char fileName[256] = {0}; 
                    ofn.lStructSize = sizeof(ofn); 
                    ofn.hwndOwner = hwnd; 
                    ofn.lpstrFile = fileName; 
                    ofn.nMaxFile = sizeof(fileName); 
                    ofn.lpstrFilter = "All Files\0*.*\0"; 
                    ofn.Flags = OFN_FILEMUSTEXIST; 
                    if (GetOpenFileName(&ofn)) { 
                        cleanup_editor(&editor); 
                        init_file(&editor, fileName); 
                    }
                    break;
                }
                case 1: save_dirty(&editor.tracker, editor.fp); break; // Save
                case 2: editor.edit_mode = 1 - editor.edit_mode; hex_state = 0; break; // Mode
                case 3: { // BPR
                    int bprs[] = {8, 16, 24, 32, 48}; 
                    int idx = 0; 
                    for(int k=0; k<5; k++) if(bprs[k] == editor.bytes_per_row) idx = k; 
                    editor.bytes_per_row = bprs[(idx+1)%5]; 
                    SnapWindowSize(hwnd, visibleRows); // Re-align on BPR change
                    break;
                }
                case 4: editor.view_layout = 1 - editor.view_layout; break; // View
                case 5: DestroyWindow(hwnd); break; // Exit
                default: 
                    ReleaseCapture(); 
                    SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); 
                    break;
            }
        } else {
            ReleaseCapture(); 
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0); 
        }
        InvalidateRect(hwnd, NULL, TRUE); 
        return;
    }

    // Grid Hit-Testing & Selection
    int row = (yPos - MENU_HEIGHT) / charHeight;
    size_t offset = editor.view_offset + (row * bpr);
    int xHex = 10 * charWidth; 
    int xAscii = (editor.view_layout == 0) ? (xHex + (bpr * 3 + 2) * charWidth) : xHex;
    int col = -1;

    if (editor.view_layout == 0 && xPos >= xHex && xPos < xAscii - 2*charWidth) { 
        col = (xPos - xHex) / (3 * charWidth); 
        editor.edit_mode = 0; 
    } else if (xPos >= xAscii) { 
        col = (xPos - xAscii) / charWidth; 
        editor.edit_mode = 1; 
    }

    if (col >= 0 && col < bpr && offset + col < editor.file_size) { 
        editor.cursor = offset + col; 
        
        // Handle Shift+Click or Drag Selection
        if (wParam & MK_LBUTTON) {
            editor.selection_end = editor.cursor;
        } else if (!(GetKeyState(VK_SHIFT) & 0x8000)) {
            editor.selection_start = editor.cursor;
            editor.selection_end = editor.cursor;
        } else {
            editor.selection_end = editor.cursor;
        }
        
        InvalidateRect(hwnd, NULL, TRUE); 
    }
}

/* ========================================================================
 * 6. Window Procedure & Entry Point
 * ======================================================================== */

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            DragAcceptFiles(hwnd, TRUE);
            SetTimer(hwnd, 1, MENU_HIDE_DELAY, NULL);
            hFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, 
                                OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                FIXED_PITCH | FF_MODERN, "Consolas");
            if (!hFont) hFont = GetStockObject(ANSI_FIXED_FONT);
            
            HDC hdc = GetDC(hwnd); 
            SelectObject(hdc, hFont); 
            TEXTMETRIC tm; 
            GetTextMetrics(hdc, &tm);
            charWidth = tm.tmAveCharWidth; 
            charHeight = tm.tmHeight + tm.tmExternalLeading; 
            ReleaseDC(hwnd, hdc);
            
            // Requirement: Always start with exactly 16 rows aligned
            SnapWindowSize(hwnd, 16);
            break;

        case WM_SIZE:
            clientRect.right = LOWORD(lParam); 
            clientRect.bottom = HIWORD(lParam);
            visibleRows = (clientRect.bottom - MENU_HEIGHT - STATUS_HEIGHT) / charHeight;
            if (visibleRows < 1) visibleRows = 1;
            break;

        case WM_MOUSEMOVE: {
            POINTS pts = MAKEPOINTS(lParam);
            if (pts.y < MENU_HEIGHT) {
                if (!menu_visible) { menu_visible = TRUE; InvalidateRect(hwnd, NULL, TRUE); }
                SetTimer(hwnd, 1, MENU_HIDE_DELAY, NULL); 
            }
            // Allow drag-selection
            if (wParam & MK_LBUTTON) {
                HandleMouse(hwnd, wParam, lParam);
            }
            break;
        }

        case WM_TIMER:
            if (wParam == 1 && menu_visible) { 
                menu_visible = FALSE; 
                InvalidateRect(hwnd, NULL, TRUE); 
            }
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps; 
            HDC hdc = BeginPaint(hwnd, &ps);
            RenderFrame(hwnd, hdc);
            EndPaint(hwnd, &ps);
            break;
        }

        case WM_LBUTTONDOWN:
            HandleMouse(hwnd, wParam, lParam);
            break;

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            int rows = abs(delta) / WHEEL_DELTA;
            size_t scrollAmount = (size_t)rows * editor.bytes_per_row;

            if (delta > 0) { // Scroll Up
                if (editor.view_offset >= scrollAmount) editor.view_offset -= scrollAmount;
                else editor.view_offset = 0;
            } else { // Scroll Down
                editor.view_offset += scrollAmount;
                if (editor.file_size > 0) {
                    size_t maxValidOffset = ((editor.file_size - 1) / editor.bytes_per_row) * editor.bytes_per_row;
                    if (editor.view_offset > maxValidOffset) editor.view_offset = maxValidOffset;
                } else {
                    editor.view_offset = 0;
                }
            }
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam; 
            char filePath[MAX_PATH];
            DragQueryFile(hDrop, 0, filePath, MAX_PATH);
            cleanup_editor(&editor); 
            init_file(&editor, filePath);
            DragFinish(hDrop);
            InvalidateRect(hwnd, NULL, TRUE); 
            break;
        }

        case WM_KEYDOWN:
            HandleKeyboard(hwnd, wParam, lParam);
            break;

        case WM_CHAR:
            HandleCharInput(hwnd, wParam);
            break;

        case WM_DESTROY:
            KillTimer(hwnd, 1); 
            cleanup_editor(&editor); 
            DeleteObject(hFont); 
            PostQuitMessage(0); 
            break;
            
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wc = {0}; 
    wc.cbSize = sizeof(WNDCLASSEX); 
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc; 
    wc.hInstance = hInstance; 
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 30)); 
    wc.lpszClassName = "HexEditorClass";
    RegisterClassEx(&wc);

    // Requirement: WS_OVERLAPPEDWINDOW allows free expansion/resizing by the user
    HWND hwnd = CreateWindowEx(WS_EX_ACCEPTFILES, "HexEditorClass", "Hex Editor",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
                               NULL, NULL, hInstance, NULL);

    editor.bytes_per_row = 16;
    editor.view_layout = 0; // Default to Offset + Hex + ASCII
    
    if (__argc > 1) init_file(&editor, __argv[1]);
    else init_file(&editor, "untitled.bin");

    ShowWindow(hwnd, nCmdShow); 
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { 
        TranslateMessage(&msg); 
        DispatchMessage(&msg); 
    }
    return (int)msg.wParam;
}
