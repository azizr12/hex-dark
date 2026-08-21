#define _WIN32_WINNT 0x0600

#include <windows.h>
#include <uxtheme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "ui.h"
#include "features.h"
#include "window_fix.h"

HexEditor editor;
int hex_state = 0;
uint8_t temp_hex = 0;
HFONT hFont = NULL;
int charWidth = 8;
int charHeight = 19;
int visibleRows = 16;
RECT clientRect = {0};
RECT fullClientRect = {0};
HWND g_hScroll = NULL;

size_t get_virtual_size(void) {
    size_t s = get_effective_size(&editor);
    if (!editor.readonly_mode) {
        if (editor.cursor > s) s = editor.cursor;
        if (s < SIZE_MAX) s += 1;
    }
    return s;
}

size_t get_total_rows(void) {
    int bpr = editor.bytes_per_row > 0 ? editor.bytes_per_row : 16;
    size_t vs = get_virtual_size();
    size_t rows;
    if (vs > SIZE_MAX - (size_t)bpr) rows = SIZE_MAX / (size_t)bpr;
    else rows = (vs + (size_t)bpr - 1) / (size_t)bpr;
    if (rows == 0) rows = 1;
    return rows;
}

void initialize_editor(int argc, char **argv) {
    memset(&editor, 0, sizeof(editor));
    if (argc > 1) {
        if (init_file(&editor, argv[1]) != 0) init_memory_mode(&editor);
    } else {
        init_memory_mode(&editor);
    }
    editor.bytes_per_row = cfg.bytes_per_row;
    editor.view_layout = cfg.view_layout;
    editor.edit_mode = cfg.edit_mode;
    editor.readonly_mode = 0;
    editor.selection_mode = editor.edit_mode;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow) {
    (void)hPrev;
    (void)lpCmd;
    load_config();
    initialize_editor(__argc, __argv);

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = "HexEditorClass";
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_ACCEPTFILES | WS_EX_COMPOSITED,
        "HexEditorClass",
        "Hex Editor",
        WS_POPUP | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 1000,
        NULL, NULL, hInst, NULL);

    if (hwnd != NULL) {
        SetWindowTheme(hwnd, L"DarkMode_Explorer", NULL);
        FeaturesInstall(hwnd);
        FeaturesApplyDarkScrollbar(hwnd);
        WindowFixInstall(hwnd);
        ShowWindow(hwnd, nShow);
        UpdateWindow(hwnd);
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
