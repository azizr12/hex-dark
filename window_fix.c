#include "window_fix.h"
#include "app.h"
#include "ui.h"

static WNDPROC previous_proc;

static LRESULT CALLBACK WindowFixProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_LBUTTONDOWN && GET_Y_LPARAM(lParam) < 35) {
        int block = GET_X_LPARAM(lParam) / (8 * charWidth);
        if (block == 9) {
            DestroyWindow(hwnd);
            return 0;
        }
    }
    return CallWindowProcA(previous_proc, hwnd, msg, wParam, lParam);
}

void WindowFixInstall(HWND hwnd) {
    previous_proc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)WindowFixProc);
}
