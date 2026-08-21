#include "config.h"
#include <stdio.h>
#include <string.h>

AppConfig cfg;
char ini_path[MAX_PATH];

static COLORREF ini_get_color(const char *sec, const char *key, COLORREF def) {
    char buf[64] = {0};
    GetPrivateProfileStringA(sec, key, "", buf, sizeof(buf), ini_path);
    int r, g, b;
    if (sscanf(buf, "%d,%d,%d", &r, &g, &b) == 3) return RGB(r, g, b);
    return def;
}

static void ini_put_color(const char *sec, const char *key, COLORREF c) {
    char buf[64];
    sprintf(buf, "%d,%d,%d", GetRValue(c), GetGValue(c), GetBValue(c));
    WritePrivateProfileStringA(sec, key, buf, ini_path);
}

static void generate_default_ini(void) {
    WritePrivateProfileStringA("Window", "rows", "16", ini_path);
    WritePrivateProfileStringA("Window", "aspect_w", "16", ini_path);
    WritePrivateProfileStringA("Window", "aspect_h", "9", ini_path);
    WritePrivateProfileStringA("Font", "name", "Consolas", ini_path);
    WritePrivateProfileStringA("Font", "size", "16", ini_path);

    ini_put_color("Colors", "background", RGB(30,30,30));
    ini_put_color("Colors", "menu_bg", RGB(45,45,45));
    ini_put_color("Colors", "menu_text", RGB(220,220,220));
    ini_put_color("Colors", "menu_hidden", RGB(100,100,100));
    ini_put_color("Colors", "offset", RGB(0,255,255));
    ini_put_color("Colors", "hex", RGB(0,255,0));
    ini_put_color("Colors", "ascii", RGB(255,255,0));
    ini_put_color("Colors", "selection", RGB(0,50,150));
    ini_put_color("Colors", "cursor", RGB(0,150,255));
    ini_put_color("Colors", "status_bg", RGB(40,40,40));
    ini_put_color("Colors", "status_text", RGB(180,180,180));
    ini_put_color("Colors", "readonly_btn", RGB(0,180,0));
    ini_put_color("Colors", "overwrite_btn", RGB(200,0,0));

    WritePrivateProfileStringA("Behavior", "menu_hide_delay", "2000", ini_path);
    WritePrivateProfileStringA("Behavior", "bytes_per_row", "16", ini_path);
    WritePrivateProfileStringA("Behavior", "view_layout", "0", ini_path);
    WritePrivateProfileStringA("Behavior", "edit_mode", "0", ini_path);
    WritePrivateProfileStringA("Engine", "tracker_initial_capacity", "65536", ini_path);
}

void load_config(void) {
    char exe_dir[MAX_PATH];
    GetModuleFileNameA(NULL, exe_dir, MAX_PATH);
    char *slash = strrchr(exe_dir, '\\');
    if (slash) *(slash + 1) = '\0';
    snprintf(ini_path, MAX_PATH, "%sconfig.ini", exe_dir);

    if (GetFileAttributesA(ini_path) == INVALID_FILE_ATTRIBUTES) generate_default_ini();

    cfg.rows = GetPrivateProfileIntA("Window", "rows", 16, ini_path);
    cfg.aspect_w = GetPrivateProfileIntA("Window", "aspect_w", 16, ini_path);
    cfg.aspect_h = GetPrivateProfileIntA("Window", "aspect_h", 9, ini_path);
    GetPrivateProfileStringA("Font", "name", "Consolas", cfg.font_name, sizeof(cfg.font_name), ini_path);
    cfg.font_size = GetPrivateProfileIntA("Font", "size", 16, ini_path);

    cfg.col_background = ini_get_color("Colors", "background", RGB(30,30,30));
    cfg.col_menu_bg = ini_get_color("Colors", "menu_bg", RGB(45,45,45));
    cfg.col_menu_text = ini_get_color("Colors", "menu_text", RGB(220,220,220));
    cfg.col_menu_hidden = ini_get_color("Colors", "menu_hidden", RGB(100,100,100));
    cfg.col_offset = ini_get_color("Colors", "offset", RGB(0,255,255));
    cfg.col_hex = ini_get_color("Colors", "hex", RGB(0,255,0));
    cfg.col_ascii = ini_get_color("Colors", "ascii", RGB(255,255,0));
    cfg.col_selection = ini_get_color("Colors", "selection", RGB(0,50,150));
    cfg.col_cursor = ini_get_color("Colors", "cursor", RGB(0,150,255));
    cfg.col_status_bg = ini_get_color("Colors", "status_bg", RGB(40,40,40));
    cfg.col_status_text = ini_get_color("Colors", "status_text", RGB(180,180,180));
    cfg.col_ro_btn = ini_get_color("Colors", "readonly_btn", RGB(0,180,0));
    cfg.col_rw_btn = ini_get_color("Colors", "overwrite_btn", RGB(200,0,0));

    cfg.menu_hide_delay = GetPrivateProfileIntA("Behavior", "menu_hide_delay", 2000, ini_path);
    cfg.bytes_per_row = GetPrivateProfileIntA("Behavior", "bytes_per_row", 16, ini_path);
    cfg.view_layout = GetPrivateProfileIntA("Behavior", "view_layout", 0, ini_path);
    cfg.edit_mode = GetPrivateProfileIntA("Behavior", "edit_mode", 0, ini_path);
    cfg.tracker_cap = GetPrivateProfileIntA("Engine", "tracker_initial_capacity", 65536, ini_path);

    if (cfg.font_name[0] == '\0') strncpy(cfg.font_name, "Consolas", sizeof(cfg.font_name) - 1);
    if (cfg.rows < 1) cfg.rows = 16;
    if (cfg.aspect_w < 1) cfg.aspect_w = 16;
    if (cfg.aspect_h < 1) cfg.aspect_h = 9;
    if (cfg.font_size < 1) cfg.font_size = 16;
    if (cfg.menu_hide_delay < 1) cfg.menu_hide_delay = 2000;
    if (cfg.bytes_per_row < 1) cfg.bytes_per_row = 16;
    if (cfg.view_layout != 1) cfg.view_layout = 0;
    if (cfg.edit_mode != 1) cfg.edit_mode = 0;
    if (cfg.tracker_cap < 64) cfg.tracker_cap = 65536;
}