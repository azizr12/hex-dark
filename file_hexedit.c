#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <ncurses.h>

/* ========================================================================
 * Configuration & Data Structures
 * ======================================================================== */

#define WINDOW_SIZE 4096  /* Size of the sliding file cache in bytes */

typedef struct {
    FILE *fp;             /* File pointer */
    size_t file_size;     /* Total size of the file in bytes */
    size_t cursor;        /* Current absolute cursor position in the file */
    size_t view_offset;   /* Absolute offset of the top-left viewport */
    
    uint8_t window[WINDOW_SIZE]; /* Sliding window cache */
    size_t window_start;  /* File offset where the window begins */
    size_t window_len;    /* Actual bytes currently loaded in the window */
    
    char filename[256];   /* Path to the target file */
} HexEditor;

/* ========================================================================
 * File I/O & Window Management
 * ======================================================================== */

int init_file(HexEditor *ed, const char *filename) {
    ed->fp = fopen(filename, "r+b"); /* Open for reading and writing */
    if (!ed->fp) {
        /* If file doesn't exist, create it */
        ed->fp = fopen(filename, "w+b");
        if (!ed->fp) {
            perror("Failed to open or create file");
            return -1;
        }
    }

    fseek(ed->fp, 0, SEEK_END);
    ed->file_size = ftell(ed->fp);
    fseek(ed->fp, 0, SEEK_SET);

    strncpy(ed->filename, filename, sizeof(ed->filename) - 1);
    ed->window_start = 0;
    ed->window_len = 0;
    
    return 0;
}

void load_window(HexEditor *ed, size_t target_offset) {
    /* Align window start to a 16-byte boundary for clean rendering */
    ed->window_start = (target_offset / 16) * 16;
    
    fseek(ed->fp, ed->window_start, SEEK_SET);
    ed->window_len = fread(ed->window, 1, WINDOW_SIZE, ed->fp);
}

uint8_t get_byte(HexEditor *ed, size_t offset) {
    if (offset >= ed->file_size) return 0;
    
    /* If offset is outside the current window, reload the window */
    if (offset < ed->window_start || offset >= ed->window_start + ed->window_len) {
        load_window(ed, offset);
    }
    
    return ed->window[offset - ed->window_start];
}

void set_byte(HexEditor *ed, size_t offset, uint8_t value) {
    if (offset >= ed->file_size) return;

    /* Write directly to the file */
    fseek(ed->fp, offset, SEEK_SET);
    fwrite(&value, 1, 1, ed->fp);
    fflush(ed->fp);

    /* Update the local window cache if the offset falls within it */
    if (offset >= ed->window_start && offset < ed->window_start + ed->window_len) {
        ed->window[offset - ed->window_start] = value;
    }
    
    /* If the file was extended (not applicable in strict overwrite mode, 
       but useful if insert mode is added later), update file_size */
}

void cleanup_editor(HexEditor *ed) {
    if (ed->fp) {
        fclose(ed->fp);
    }
}

/* ========================================================================
 * Rendering Engine
 * ======================================================================== */

void draw_screen(HexEditor *ed) {
    clear();
    int max_rows = LINES - 2; /* Reserve bottom line for status bar */
    if (max_rows <= 0) max_rows = 1;

    for (int i = 0; i < max_rows; i++) {
        size_t offset = ed->view_offset + (i * 16);
        if (offset >= ed->file_size && ed->file_size > 0) break;

        /* 1. Print Offset Address */
        attron(COLOR_PAIR(1));
        printw("%08ZX  ", offset);
        attroff(COLOR_PAIR(1));

        /* 2. Print Hexadecimal Bytes */
        for (int j = 0; j < 16; j++) {
            if (offset + j < ed->file_size) {
                if (offset + j == ed->cursor) {
                    attron(A_REVERSE | A_BOLD);
                }
                printw("%02X ", get_byte(ed, offset + j));
                if (offset + j == ed->cursor) {
                    attroff(A_REVERSE | A_BOLD);
                }
            } else {
                printw("   ");
            }
            if (j == 7) printw(" "); /* 8-byte visual separator */
        }

        printw(" |");

        /* 3. Print ASCII Representation */
        for (int j = 0; j < 16; j++) {
            if (offset + j < ed->file_size) {
                uint8_t c = get_byte(ed, offset + j);
                if (offset + j == ed->cursor) {
                    attron(A_REVERSE | A_BOLD);
                }
                printw("%c", isprint(c) ? c : '.');
                if (offset + j == ed->cursor) {
                    attroff(A_REVERSE | A_BOLD);
                }
            } else {
                printw(" ");
            }
        }
        printw("|\n");
    }

    /* Status Bar */
    attron(A_REVERSE);
    mvprintw(LINES - 1, 0, " File: %-30s | Size: %10zu bytes | Offset: %08ZX | F2: Force Save | q: Quit ",
             ed->filename, ed->file_size, ed->cursor);
    attroff(A_REVERSE);

    refresh();
}

/* ========================================================================
 * Input Handling & Viewport Logic
 * ======================================================================== */

void update_viewport(HexEditor *ed) {
    int max_rows = LINES - 2;
    if (max_rows <= 0) max_rows = 1;
    
    size_t cursor_row = ed->cursor / 16;
    size_t view_start_row = ed->view_offset / 16;

    if (cursor_row < view_start_row) {
        ed->view_offset = cursor_row * 16;
    } else if (cursor_row >= view_start_row + max_rows) {
        ed->view_offset = (cursor_row - max_rows + 1) * 16;
    }
}

void handle_input(HexEditor *ed) {
    int ch = getch();
    static int hex_state = 0;
    static uint8_t temp_hex = 0;

    switch (ch) {
        case 'q':
        case 'Q':
            cleanup_editor(ed);
            endwin();
            exit(0);

        case KEY_UP:
            if (ed->cursor >= 16) ed->cursor -= 16;
            hex_state = 0;
            break;

        case KEY_DOWN:
            if (ed->cursor + 16 < ed->file_size) ed->cursor += 16;
            hex_state = 0;
            break;

        case KEY_LEFT:
            if (ed->cursor > 0) ed->cursor--;
            hex_state = 0;
            break;

        case KEY_RIGHT:
            if (ed->cursor + 1 < ed->file_size) ed->cursor++;
            hex_state = 0;
            break;

        case KEY_PPAGE: /* Page Up */
            if (ed->cursor >= 16 * (LINES - 2)) {
                ed->cursor -= 16 * (LINES - 2);
            } else {
                ed->cursor = 0;
            }
            hex_state = 0;
            break;

        case KEY_NPAGE: /* Page Down */
            if (ed->cursor + 16 * (LINES - 2) < ed->file_size) {
                ed->cursor += 16 * (LINES - 2);
            } else {
                ed->cursor = ed->file_size - 1;
            }
            hex_state = 0;
            break;

        case KEY_F2: /* Force flush to disk */
            fflush(ed->fp);
            break;

        default:
            if (ed->file_size > 0) {
                int val = -1;
                if (ch >= '0' && ch <= '9') val = ch - '0';
                else if (ch >= 'a' && ch <= 'f') val = ch - 'a' + 10;
                else if (ch >= 'A' && ch <= 'F') val = ch - 'A' + 10;

                if (val != -1) {
                    if (hex_state == 0) {
                        temp_hex = (uint8_t)(val << 4);
                        hex_state = 1;
                    } else {
                        uint8_t final_byte = temp_hex | (uint8_t)val;
                        set_byte(ed, ed->cursor, final_byte);
                        hex_state = 0;
                        if (ed->cursor + 1 < ed->file_size) ed->cursor++;
                    }
                } else {
                    hex_state = 0; /* Reset state on invalid hex input */
                }
            }
            break;
    }
    update_viewport(ed);
}

/* ========================================================================
 * Main Execution
 * ======================================================================== */

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return EXIT_FAILURE;
    }

    HexEditor editor = {0};
    if (init_file(&editor, argv[1]) != 0) {
        return EXIT_FAILURE;
    }

    /* Initialize ncurses with color support */
    initscr();
    start_color();
    init_pair(1, COLOR_CYAN, COLOR_BLACK); /* Offset address color */
    raw();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0); /* Hide default terminal cursor */

    /* Main Event Loop */
    while (1) {
        draw_screen(&editor);
        handle_input(&editor);
    }

    return EXIT_SUCCESS;
}
