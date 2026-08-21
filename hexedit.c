#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <ncurses.h>

#ifdef _WIN32
#include <windows.h>
#define LOAD_LIB(name) LoadLibraryA(name)
#define GET_SYM(lib, name) GetProcAddress((HMODULE)lib, name)
#define FREE_LIB(lib) FreeLibrary((HMODULE)lib)
#else
#include <dlfcn.h>
#define LOAD_LIB(name) dlopen(name, RTLD_LAZY)
#define GET_SYM(lib, name) dlsym(lib, name)
#define FREE_LIB(lib) dlclose(lib)
#endif

/* ========================================================================
 * Data Structures
 * ======================================================================== */

#define WINDOW_SIZE 4096

typedef struct {
    size_t offset;
    uint8_t original;
    uint8_t modified;
} DirtyByte;

typedef struct {
    DirtyByte *items;
    size_t count;
    size_t capacity;
} DirtyTracker;

typedef uint8_t (*TranslateFunc)(uint8_t);

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
    
    void *plugin_handle;
    TranslateFunc plugin_translate;
} HexEditor;

/* ========================================================================
 * Utility & Plugin Functions
 * ======================================================================== */

int hex_char_to_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void load_plugin(HexEditor *ed) {
    ed->plugin_handle = NULL;
    ed->plugin_translate = NULL;
    
#ifdef _WIN32
    ed->plugin_handle = LoadLibraryA("hexedit_plugin.dll");
#else
    ed->plugin_handle = dlopen("./hexedit_plugin.so", RTLD_LAZY);
#endif

    if (ed->plugin_handle) {
#ifdef _WIN32
        ed->plugin_translate = (TranslateFunc)GetProcAddress((HMODULE)ed->plugin_handle, "translate_byte");
#else
        ed->plugin_translate = (TranslateFunc)dlsym(ed->plugin_handle, "translate_byte");
#endif
    }
}

uint8_t translate_char(HexEditor *ed, uint8_t b) {
    if (ed->plugin_translate) return ed->plugin_translate(b);
    return isprint(b) ? b : '.';
}

/* ========================================================================
 * File I/O & Dirty Tracking
 * ======================================================================== */

void init_tracker(DirtyTracker *t) {
    t->capacity = 1024;
    t->count = 0;
    t->items = malloc(t->capacity * sizeof(DirtyByte));
}

void load_window(HexEditor *ed, size_t target_offset) {
    int bpr = ed->bytes_per_row;
    ed->window_start = (target_offset / bpr) * bpr;
    fseek(ed->fp, ed->window_start, SEEK_SET);
    ed->window_len = fread(ed->window, 1, WINDOW_SIZE, ed->fp);
}

uint8_t get_byte(HexEditor *ed, size_t offset) {
    if (offset >= ed->file_size) return 0;
    
    // 1. Check dirty tracker first
    for (size_t i = 0; i < ed->tracker.count; i++) {
        if (ed->tracker.items[i].offset == offset) {
            return ed->tracker.items[i].modified;
        }
    }
    
    // 2. Fallback to window
    if (offset < ed->window_start || offset >= ed->window_start + ed->window_len) {
        load_window(ed, offset);
    }
    return ed->window[offset - ed->window_start];
}

void set_byte(HexEditor *ed, size_t offset, uint8_t value) {
    if (offset >= ed->file_size) return;
    
    // Check if already dirty
    for (size_t i = 0; i < ed->tracker.count; i++) {
        if (ed->tracker.items[i].offset == offset) {
            ed->tracker.items[i].modified = value;
            return;
        }
    }
    
    // Read true original from file/window
    if (offset < ed->window_start || offset >= ed->window_start + ed->window_len) {
        load_window(ed, offset);
    }
    uint8_t true_orig = ed->window[offset - ed->window_start];
    
    // Add to tracker
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
        fseek(fp, t->items[i].offset, SEEK_SET);
        if (fwrite(&t->items[i].modified, 1, 1, fp) != 1) return -1;
    }
    fflush(fp);
    t->count = 0;
    return 0;
}

int init_file(HexEditor *ed, const char *filename) {
    ed->fp = fopen(filename, "r+b");
    if (!ed->fp) {
        ed->fp = fopen(filename, "w+b");
        if (!ed->fp) return -1;
    }
    fseek(ed->fp, 0, SEEK_END);
    ed->file_size = ftell(ed->fp);
    fseek(ed->fp, 0, SEEK_SET);
    strncpy(ed->filename, filename, sizeof(ed->filename) - 1);
    init_tracker(&ed->tracker);
    load_plugin(ed);
    return 0;
}

void cleanup_editor(HexEditor *ed) {
    if (ed->fp) fclose(ed->fp);
    free(ed->tracker.items);
    if (ed->plugin_handle) FREE_LIB(ed->plugin_handle);
}

/* ========================================================================
 * Rendering Engine (Dark Mode)
 * ======================================================================== */

void draw_screen(HexEditor *ed) {
    clear();
    int max_rows = LINES - 2;
    if (max_rows <= 0) max_rows = 1;
    int bpr = ed->bytes_per_row;

    for (int i = 0; i < max_rows; i++) {
        size_t offset = ed->view_offset + (i * bpr);
        if (offset >= ed->file_size && ed->file_size > 0) break;

        attron(COLOR_PAIR(1)); // Cyan for offset
        printw("%08ZX  ", offset);
        attroff(COLOR_PAIR(1));

        attron(COLOR_PAIR(2)); // Green for hex
        for (int j = 0; j < bpr; j++) {
            if (offset + j < ed->file_size) {
                if (offset + j == ed->cursor && ed->edit_mode == 0) {
                    attron(COLOR_PAIR(4)); // Highlight
                    printw("%02X", get_byte(ed, offset + j));
                    attroff(COLOR_PAIR(4));
                } else {
                    printw("%02X", get_byte(ed, offset + j));
                }
            } else {
                printw("  ");
            }
            printw(" ");
            if (j == (bpr / 2) - 1 && bpr > 8) printw(" ");
        }
        attroff(COLOR_PAIR(2));

        printw(" | ");

        attron(COLOR_PAIR(3)); // Yellow for ASCII
        for (int j = 0; j < bpr; j++) {
            if (offset + j < ed->file_size) {
                uint8_t c = get_byte(ed, offset + j);
                uint8_t tc = translate_char(ed, c);
                if (offset + j == ed->cursor && ed->edit_mode == 1) {
                    attron(COLOR_PAIR(4));
                    printw("%c", tc);
                    attroff(COLOR_PAIR(4));
                } else {
                    printw("%c", tc);
                }
            } else {
                printw(" ");
            }
        }
        attroff(COLOR_PAIR(3));
        printw("\n");
    }

    attron(A_REVERSE);
    mvprintw(LINES - 1, 0, " %.20s | Size: %zu | Off: %08ZX | Mode: %s | BPR: %d | Dirty: %zu | F2:Save F3:BPR F5:Search Tab:Mode q:Quit ",
             ed->filename, ed->file_size, ed->cursor, 
             ed->edit_mode == 0 ? "HEX " : "TEXT",
             ed->bytes_per_row, ed->tracker.count);
    attroff(A_REVERSE);
    refresh();
}

/* ========================================================================
 * Search & Input Handling
 * ======================================================================== */

void do_search(HexEditor *ed) {
    echo(); curs_set(1);
    attron(A_REVERSE);
    mvprintw(LINES - 1, 0, " Search Mode: [H]ex or [T]ext? ");
    clrtoeol();
    refresh();
    
    int mode_ch = getch();
    int is_hex = (mode_ch == 'h' || mode_ch == 'H');
    
    mvprintw(LINES - 1, 0, " Enter %s pattern [Esc to cancel]: ", is_hex ? "Hex" : "Text");
    clrtoeol();
    refresh();
    
    char input[256];
    int i = 0;
    while (1) {
        int ch = getch();
        if (ch == 27) break; 
        if (ch == '\n' || ch == '\r') { input[i] = '\0'; break; }
        if (ch == KEY_BACKSPACE || ch == 127) {
            if (i > 0) { i--; mvaddch(LINES - 1, 32 + i, ' '); move(LINES - 1, 32 + i); }
        } else if (i < 255) {
            input[i++] = ch; mvaddch(LINES - 1, 32 + i - 1, ch);
        }
    }
    noecho(); curs_set(0);
    if (i == 0) return;
    
    uint8_t pattern[256];
    size_t pat_len = 0;
    
    if (is_hex) {
        for (size_t j = 0; j < i; j++) {
            if (input[j] == ' ') continue;
            if (j + 1 >= i) break;
            int h = hex_char_to_val(input[j]);
            int l = hex_char_to_val(input[j+1]);
            if (h != -1 && l != -1) pattern[pat_len++] = (h << 4) | l;
            j++;
        }
    } else {
        memcpy(pattern, input, i);
        pat_len = i;
    }
    
    size_t found = -1;
    for (size_t off = 0; off <= ed->file_size - pat_len; off++) {
        int match = 1;
        for (size_t k = 0; k < pat_len; k++) {
            if (get_byte(ed, off + k) != pattern[k]) { match = 0; break; }
        }
        if (match) { found = off; break; }
    }
    
    if (found != (size_t)-1) {
        ed->cursor = found;
        ed->view_offset = (found / ed->bytes_per_row) * ed->bytes_per_row;
    } else {
        attron(A_REVERSE | A_BOLD);
        mvprintw(LINES - 1, 0, " NOT FOUND! Press any key... ");
        attroff(A_REVERSE | A_BOLD);
        refresh(); getch();
    }
}

void handle_input(HexEditor *ed) {
    int ch = getch();
    static int hex_state = 0;
    static uint8_t temp_hex = 0;
    int bpr = ed->bytes_per_row;

    switch (ch) {
        case 'q': case 'Q':
            cleanup_editor(ed); endwin(); exit(0);
        case '\t': // Tab
            ed->edit_mode = 1 - ed->edit_mode; hex_state = 0; break;
        case KEY_F3: // Cycle BPR
            { int bprs[] = {8, 16, 24, 32, 48}; int idx = 0;
              for(int k=0; k<5; k++) if(bprs[k] == bpr) idx = k;
              ed->bytes_per_row = bprs[(idx + 1) % 5]; hex_state = 0; } break;
        case KEY_F2: // Save
            if (save_dirty(&ed->tracker, ed->fp) == 0) { /* Success */ } break;
        case KEY_F5: // Search
            do_search(ed); break;
            
        case KEY_UP:
            if (ed->cursor >= bpr) ed->cursor -= bpr; hex_state = 0; break;
        case KEY_DOWN:
            if (ed->cursor + bpr < ed->file_size) ed->cursor += bpr; hex_state = 0; break;
        case KEY_LEFT:
            if (ed->cursor > 0) ed->cursor--; hex_state = 0; break;
        case KEY_RIGHT:
            if (ed->cursor + 1 < ed->file_size) ed->cursor++; hex_state = 0; break;

        default:
            if (ed->file_size > 0) {
                if (ed->edit_mode == 0) { // HEX MODE
                    int val = hex_char_to_val(ch);
                    if (val != -1) {
                        if (hex_state == 0) { temp_hex = (uint8_t)(val << 4); hex_state = 1; }
                        else {
                            set_byte(ed, ed->cursor, temp_hex | (uint8_t)val);
                            hex_state = 0;
                            if (ed->cursor + 1 < ed->file_size) ed->cursor++;
                        }
                    } else { hex_state = 0; }
                } else { // ASCII MODE
                    if (isprint(ch)) {
                        set_byte(ed, ed->cursor, (uint8_t)ch);
                        if (ed->cursor + 1 < ed->file_size) ed->cursor++;
                    }
                }
            }
            break;
    }
    
    // Update viewport
    int max_rows = LINES - 2;
    size_t cursor_row = ed->cursor / bpr;
    size_t view_start_row = ed->view_offset / bpr;
    if (cursor_row < view_start_row) ed->view_offset = cursor_row * bpr;
    else if (cursor_row >= view_start_row + max_rows) ed->view_offset = (cursor_row - max_rows + 1) * bpr;
}

/* ========================================================================
 * Main Execution
 * ======================================================================== */

int main(int argc, char *argv[]) {
    if (argc != 2) { fprintf(stderr, "Usage: %s <filename>\n", argv[0]); return 1; }

    HexEditor editor = {0};
    editor.bytes_per_row = 16;
    if (init_file(&editor, argv[1]) != 0) { fprintf(stderr, "Error opening file\n"); return 1; }

    initscr(); start_color(); use_default_colors();
    init_pair(1, COLOR_CYAN, COLOR_BLACK);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);
    init_pair(4, COLOR_BLACK, COLOR_CYAN); // Cursor highlight
    
    raw(); keypad(stdscr, TRUE); noecho(); curs_set(0);

    while (1) { draw_screen(&editor); handle_input(&editor); }
    return 0;
}
