#ifndef APP_H
#define APP_H

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
#include "config.h"

// Global State
extern HexEditor editor;
extern int hex_state;
extern uint8_t temp_hex;
extern HFONT hFont;

extern int charWidth;
extern int charHeight;
extern int visibleRows;

extern RECT clientRect;
extern RECT fullClientRect;
extern HWND g_hScroll;

// Shared Functions
void initialize_editor(int argc, char **argv);
size_t get_virtual_size(void);
size_t get_total_rows(void);
void EnsureCursorVisible(void);

#endif // APP_H