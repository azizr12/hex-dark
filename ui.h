#ifndef UI_H
#define UI_H

#include <windows.h>

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void CreateVScrollBar(HWND hwnd);
void OnSize(HWND hwnd, int cx, int cy);
void UpdateVScroll(HWND hwnd);
void SnapWindowSize(HWND hwnd, int rows);

void ClampBPRForLayout(HWND hwnd);
void CycleBPR(HWND hwnd);

#endif // UI_H