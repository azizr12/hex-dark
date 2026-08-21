#include <windows.h>
#include <commdlg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ========================================================================
 * Core Data Structures & File I/O (RAM-Only Sliding Window)
 * ======================================================================== */

#define WINDOW_SIZE 4096

typedef struct { size_t offset; uint8_t original; uint8_t modified; } DirtyByte;
typedef struct { DirtyByte *items; size_t count; size_t capacity; } DirtyTracker;

typedef struct {
    FILE *fp;
    size_t file_size;
    size_t cursor;
    size_t view_offset;
    int bytes_per_row;
    int edit_mode; /* 0 = HEX, 1 = ASCII */
    uint8_t window[WINDOW_SIZE];
    size_t window_start;
    size_t window_len;
    char filename[256];
    DirtyTracker tracker;
} HexEditor;

void init_tracker(DirtyTracker *t) { t->capacity = 1024; t->count = 0; t->items = malloc(t->capacity * sizeof(DirtyByte)); }
void load_window(HexEditor *ed, size_t target_offset) {
    int bpr = ed->bytes_per_row;
    ed->window_start = (target_offset / bpr) * bpr;
    fseek(ed->fp, (long)ed->window_start, SEEK_SET);
    ed->window_len = fread(ed->window, 1, WINDOW_SIZE, ed->fp);
}
uint8_t get_byte(HexEditor *ed, size_t offset) {
    if (offset >= ed->file_size) return 0;
    for (size_t i = 0; i < ed->tracker.count; i++) if (ed->tracker.items[i].offset == offset) return ed->tracker.items[i].modified;
    if (offset < ed->window_start || offset >= ed->window_start + ed->window_len) load_window(ed, offset);
    return ed->window[offset - ed->window_start];
}
void set_byte(HexEditor *ed, size_t offset, uint8_t value) {
    if (offset >= ed->file_size) return;
    for (size_t i = 0; i < ed->tracker.count; i++) {
        if (ed->tracker.items[i].offset == offset) { ed->tracker.items[i].modified = value; return; }
    }
    if (offset < ed->window_start || offset >= ed->window_start + ed->window_len) load_window(ed, offset);
    uint8_t true_orig = ed->window[offset - ed->window_start];
    if (ed->tracker.count == ed->tracker.capacity) {
        ed->tracker.capacity *= 2;
        ed->tracker.items = realloc(ed->tracker.items, ed->tracker.capacity * sizeof(DirtyByte));
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
    if (!ed->fp) { ed->fp = fopen(filename, "w+b"); if (!ed->fp) return -1; }
    fseek(ed->fp, 0, SEEK_END); ed->file_size = (size_t)ftell(ed->fp); fseek(ed->fp, 0, SEEK_SET);
    strncpy(ed->filename, filename, sizeof(ed->filename) - 1);
    init_tracker(&ed->tracker); return 0;
}
void cleanup_editor(HexEditor *ed) { if (ed->fp) fclose(ed->fp); free(ed->tracker.items); }

/* ========================================================================
 * GUI Globals & State
 * ======================================================================== */

HexEditor editor = {0};
static int hex_state = 0;
static uint8_t temp_hex = 0;

HFONT hFont;
int charWidth, charHeight;
int visibleRows;
RECT clientRect;

/* ========================================================================
 * Window Procedure (Event Handling & Rendering)
 * ======================================================================== */

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    int bpr = editor.bytes_per_row;

    switch (msg) {
        case WM_CREATE: {
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
            break;
        }
        case WM_SIZE:
            clientRect.right = LOWORD(lParam);
            clientRect.bottom = HIWORD(lParam);
            visibleRows = (clientRect.bottom - 25) / charHeight; // 25px for status bar
            if (visibleRows < 1) visibleRows = 1;
            break;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
            HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

            // 1. Draw Dark Background
            HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 30));
            FillRect(memDC, &clientRect, bgBrush);
            DeleteObject(bgBrush);

            SelectObject(memDC, hFont);
            SetBkMode(memDC, TRANSPARENT);

            int y = 0;
            for (int i = 0; i < visibleRows; i++) {
                size_t offset = editor.view_offset + (i * bpr);
                if (offset >= editor.file_size && editor.file_size > 0) break;

                // Draw Offset (Cyan)
                SetTextColor(memDC, RGB(0, 255, 255));
                char offStr[16];
                sprintf(offStr, "%08llX  ", (unsigned long long)offset);
                TextOut(memDC, 0, y, offStr, (int)strlen(offStr));

                int xHex = 10 * charWidth;
                // Draw Hex (Green)
                SetTextColor(memDC, RGB(0, 255, 0));
                for (int j = 0; j < bpr; j++) {
                    if (offset + j < editor.file_size) {
                        uint8_t val = get_byte(&editor, offset + j);
                        char hexStr[4]; sprintf(hexStr, "%02X ", val);
                        
                        if (offset + j == editor.cursor && editor.edit_mode == 0) {
                            RECT r = {xHex + j*3*charWidth, y, xHex + (j+1)*3*charWidth, y + charHeight};
                            FillRect(memDC, &r, CreateSolidBrush(RGB(0, 150, 255)));
                            SetTextColor(memDC, RGB(0, 0, 0));
                        }
                        TextOut(memDC, xHex + j*3*charWidth, y, hexStr, 3);
                        if (offset + j == editor.cursor && editor.edit_mode == 0) SetTextColor(memDC, RGB(0, 255, 0));
                    }
                }

                int xAscii = xHex + (bpr * 3 + 2) * charWidth;
                // Draw ASCII (Yellow)
                SetTextColor(memDC, RGB(255, 255, 0));
                for (int j = 0; j < bpr; j++) {
                    if (offset + j < editor.file_size) {
                        uint8_t c = get_byte(&editor, offset + j);
                        char ch = isprint(c) ? (char)c : '.';
                        
                        if (offset + j == editor.cursor && editor.edit_mode == 1) {
                            RECT r = {xAscii + j*charWidth, y, xAscii + (j+1)*charWidth, y + charHeight};
                            FillRect(memDC, &r, CreateSolidBrush(RGB(0, 150, 255)));
                            SetTextColor(memDC, RGB(0, 0, 0));
                        }
                        TextOut(memDC, xAscii + j*charWidth, y, &ch, 1);
                        if (offset + j == editor.cursor && editor.edit_mode == 1) SetTextColor(memDC, RGB(255, 255, 0));
                    }
                }
                y += charHeight;
            }

            // Draw Status Bar
            RECT statusRect = {0, clientRect.bottom - 25, clientRect.right, clientRect.bottom};
            FillRect(memDC, &statusRect, CreateSolidBrush(RGB(50, 50, 50)));
            SetTextColor(memDC, RGB(255, 255, 255));
            char statusStr[256];
            sprintf(statusStr, " Size: %llu | Off: %08llX | Mode: %s | BPR: %d | Dirty: %llu | F2:Save | Tab:Mode | F3:BPR ", 
                (unsigned long long)editor.file_size, (unsigned long long)editor.cursor,
                editor.edit_mode == 0 ? "HEX" : "TEXT", editor.bytes_per_row, (unsigned long long)editor.tracker.count);
            TextOut(memDC, 5, clientRect.bottom - 20, statusStr, (int)strlen(statusStr));

            // Blit to screen
            BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps);
            break;
        }

        case WM_KEYDOWN:
            switch (wParam) {
                case VK_UP: if (editor.cursor >= (size_t)bpr) editor.cursor -= bpr; break;
                case VK_DOWN: if (editor.cursor + bpr < editor.file_size) editor.cursor += bpr; break;
                case VK_LEFT: if (editor.cursor > 0) editor.cursor--; break;
                case VK_RIGHT: if (editor.cursor + 1 < editor.file_size) editor.cursor++; break;
                case VK_PRIOR: if (editor.cursor >= (size_t)(bpr * visibleRows)) editor.cursor -= bpr * visibleRows; else editor.cursor = 0; break;
                case VK_NEXT: if (editor.cursor + bpr * visibleRows < editor.file_size) editor.cursor += bpr * visibleRows; else editor.cursor = editor.file_size - 1; break;
                case VK_TAB: editor.edit_mode = 1 - editor.edit_mode; hex_state = 0; break;
                case VK_F2: save_dirty(&editor.tracker, editor.fp); break;
                case VK_F3: {
                    int bprs[] = {8, 16, 24, 32, 48}; int idx = 0;
                    for(int k=0; k<5; k++) if(bprs[k] == editor.bytes_per_row) idx = k;
                    editor.bytes_per_row = bprs[(idx+1)%5];
                } break;
            }
            // Update viewport
            size_t cursor_row = editor.cursor / bpr;
            size_t view_start_row = editor.view_offset / bpr;
            if (cursor_row < view_start_row) editor.view_offset = cursor_row * bpr;
            else if (cursor_row >= view_start_row + (size_t)visibleRows) editor.view_offset = (cursor_row - visibleRows + 1) * bpr;
            
            InvalidateRect(hwnd, NULL, TRUE);
            break;

        case WM_CHAR:
            if (editor.file_size > 0) {
                if (editor.edit_mode == 0) { // HEX
                    int val = -1; char c = (char)wParam;
                    if (c >= '0' && c <= '9') val = c - '0';
                    else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
                    if (val != -1) {
                        if (hex_state == 0) { temp_hex = (uint8_t)(val << 4); hex_state = 1; }
                        else {
                            set_byte(&editor, editor.cursor, temp_hex | (uint8_t)val);
                            hex_state = 0;
                            if (editor.cursor + 1 < editor.file_size) editor.cursor++;
                        }
                    }
                } else { // ASCII
                    if (isprint(wParam)) {
                        set_byte(&editor, editor.cursor, (uint8_t)wParam);
                        if (editor.cursor + 1 < editor.file_size) editor.cursor++;
                    }
                }
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;

        case WM_LBUTTONDOWN: {
            int xPos = LOWORD(lParam); int yPos = HIWORD(lParam);
            int row = yPos / charHeight;
            size_t offset = editor.view_offset + (row * bpr);
            int xHex = 10 * charWidth;
            int xAscii = xHex + (bpr * 3 + 2) * charWidth;
            int col = -1;

            if (xPos >= xHex && xPos < xAscii - 2*charWidth) {
                col = (xPos - xHex) / (3 * charWidth);
                editor.edit_mode = 0;
            } else if (xPos >= xAscii) {
                col = (xPos - xAscii) / charWidth;
                editor.edit_mode = 1;
            }

            if (col >= 0 && col < bpr && offset + col < editor.file_size) {
                editor.cursor = offset + col;
                InvalidateRect(hwnd, NULL, TRUE);
            }
            break;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta > 0) {
                size_t scroll = (delta / WHEEL_DELTA) * bpr;
                if (editor.view_offset >= scroll) editor.view_offset -= scroll;
                else editor.view_offset = 0;
            } else {
                editor.view_offset += ((-delta) / WHEEL_DELTA) * bpr;
                if (editor.view_offset >= editor.file_size) editor.view_offset = editor.file_size - 1;
                editor.view_offset = (editor.view_offset / bpr) * bpr;
            }
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == 101) { // Open
                OPENFILENAME ofn = {0}; char fileName[256] = {0};
                ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hwnd;
                ofn.lpstrFile = fileName; ofn.nMaxFile = sizeof(fileName);
                ofn.lpstrFilter = "All Files\0*.*\0"; ofn.nFilterIndex = 1;
                ofn.Flags = OFN_FILEMUSTEXIST;
                if (GetOpenFileName(&ofn)) {
                    cleanup_editor(&editor);
                    init_file(&editor, fileName);
                    editor.cursor = 0; editor.view_offset = 0;
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            } else if (LOWORD(wParam) == 102) { // Save
                save_dirty(&editor.tracker, editor.fp);
            } else if (LOWORD(wParam) == 103) { // Exit
                DestroyWindow(hwnd);
            }
            break;

        case WM_DESTROY:
            cleanup_editor(&editor);
            DeleteObject(hFont);
            PostQuitMessage(0);
            break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

/* ========================================================================
 * Application Entry Point
 * ======================================================================== */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "HexEditorClass";
    RegisterClassEx(&wc);

    // Menu Setup
    HMENU hMenu = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    AppendMenu(hFile, MF_STRING, 101, "&Open...\tCtrl+O");
    AppendMenu(hFile, MF_STRING, 102, "&Save\tCtrl+S");
    AppendMenu(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenu(hFile, MF_STRING, 103, "E&xit\tAlt+F4");
    AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFile, "&File");

    HWND hwnd = CreateWindowEx(0, "HexEditorClass", "RAM-Only Hex Editor (GUI)",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1000, 700,
                               NULL, hMenu, hInstance, NULL);

    // Initialize File
    editor.bytes_per_row = 16;
    if (__argc > 1) {
        init_file(&editor, __argv[1]);
    } else {
        init_file(&editor, "untitled.bin"); // Creates a 0-byte dummy file until Open is clicked
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
