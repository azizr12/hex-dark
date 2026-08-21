#include "app.h"
#include "ui.h"
#include <windows.h>

static WNDPROC previous_proc;
static int had_selection;
static int start_mode;

static LRESULT CALLBACK SelectionFixProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_LBUTTONDOWN) {
        had_selection = has_selection(&editor);
        start_mode = editor.edit_mode;
    }
    LRESULT r = CallWindowProcA(previous_proc, hwnd, msg, wParam, lParam);
    if (msg == WM_LBUTTONDOWN && !had_selection && has_selection(&editor))
        editor.selection_mode = start_mode;
    return r;
}

void SelectionFixInstall(HWND hwnd) {
    previous_proc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)SelectionFixProc);
}
