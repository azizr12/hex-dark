#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>

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

extern AppConfig cfg;
extern char ini_path[MAX_PATH];

void load_config(void);

#endif // CONFIG_H