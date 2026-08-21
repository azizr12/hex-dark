#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <ncurses.h>

/* ========================================================================
 * Data Structures
 * ======================================================================== */

typedef struct {
    uint8_t *data;          /* Pointer to the file data buffer */
    size_t size;            /* Total size of the file in bytes */
    size_t cursor;          /* Current absolute cursor position */
    size_t view_offset;     /* Absolute offset of the top-left viewport */
    int modified;           /* Flag indicating unsaved changes */
    int hex_state;          /* State machine for hex input (0: high nibble, 1: low nibble) */
    uint8_t temp_hex;       /* Temporary storage for the high nibble */
    char filename[256];     /* Path to the target file */
} HexEditor;

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

int hex_char_to_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* ========================================================================
 * File Operations
 * ======================================================================== */

int load_file(HexEditor *ed, const char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    ed->size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (ed->size == 0) {
        ed->data = NULL;
        fclose(fp);
        strncpy(ed->filename, filename, sizeof(ed->filename) - 1);
        return 0;
    }

    ed->data = (uint8_t *)malloc(ed->size);
    if (!ed->data) {
        fclose(fp);
        return -1;
    }

    if (fread(ed->data, 1, ed->size, fp) != ed->size) {
        free(ed->data);
        ed->data = NULL;
        fclose(fp);
        return -1;
    }

    fclose(fp);
    strncpy(ed->filename, filename, sizeof(ed->filename) - 1);
    return 0;
}

int save_file(HexEditor *ed) {
    if (!ed->modified || !ed->data) return 0;

    FILE *fp = fopen(ed->filename, "wb");
    if (!fp) return -1;

    if (fwrite(ed->data, 1, ed->size, fp) != ed->size) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    ed->modified = 0;
    return 0;
}

/* ========================================================================
 * Rendering Engine
 * ======================================================================== */

void draw_screen(HexEditor *ed) {
    clear();
    int max_rows = LINES - 2; /* Reserve lines for status bar and borders */
    
    for (int i = 0; i < max_rows; i++) {
        size_t offset = ed->view_offset + (i * 16);
        if (offset >= ed->size && ed->size > 0) break;

        /* Print Offset */
        printw("%08ZX  ", offset);

        /* Print Hexadecimal Representation */
        for (int j = 0; j < 16; j++) {
            if (offset + j < ed->size) {
                if (offset + j == ed->cursor) attron(A_REVERSE);
                printw("%02X ", ed->data[offset + j]);
                if (offset + j == ed->cursor) attroff(A_REVERSE);
            } else {
                printw("   ");
            }
            if (j == 7) printw(" "); /* Visual separator between 8-byte blocks */
        }

        printw(" |");

        /* Print ASCII Representation */
        for (int j = 0; j < 16; j++) {
            if (offset + j < ed->size) {
                uint8_t c = ed->data[offset + j];
                if (offset + j == ed->cursor) attron(A_REVERSE);
                printw("%c", isprint(c) ? c : '.');
                if (offset + j == ed->cursor) attroff(A_REVERSE);
            } else {
                printw(" ");
            }
        }
        printw("|\n");
    }

    /* Status Bar */
    attron(A_REVERSE);
    mvprintw(LINES - 1, 0, " File: %-20s | Size: %7zu | Offset: %08ZX | Mod: %s | Ctrl+S: Save | q: Quit ",
             ed->filename, ed->size, ed->cursor, ed->modified ? "Yes" : "No ");
    attroff(A_REVERSE);

    refresh();
}

/* ========================================================================
 * Input Handling and Viewport Management
 * ======================================================================== */

void update_viewport(HexEditor *ed) {
    int max_rows = LINES - 2;
    size_t cursor_row = ed->cursor / 16;
    size_t view_start_row = ed->view_offset / 16;

    /* Scroll up if cursor moves above viewport */
    if (cursor_row < view_start_row) {
        ed->view_offset = cursor_row * 16;
    } 
    /* Scroll down if cursor moves below viewport */
    else if (cursor_row >= view_start_row + max_rows) {
        ed->view_offset = (cursor_row - max_rows + 1) * 16;
    }
}

void handle_input(HexEditor *ed) {
    int ch = getch();

    switch (ch) {
        case 'q':
        case 'Q':
            endwin();
            if (ed->data) free(ed->data);
            exit(0);
            break;

        case KEY_UP:
            if (ed->cursor >= 16) ed->cursor -= 16;
            ed->hex_state = 0; /* Reset hex input state on navigation */
            break;

        case KEY_DOWN:
            if (ed->cursor + 16 < ed->size) ed->cursor += 16;
            ed->hex_state = 0;
            break;

        case KEY_LEFT:
            if (ed->cursor > 0) ed->cursor--;
            ed->hex_state = 0;
            break;

        case KEY_RIGHT:
            if (ed->cursor + 1 < ed->size) ed->cursor++;
            ed->hex_state = 0;
            break;

        case 19: /* Ctrl+S */
            if (save_file(ed) == 0) {
                /* Optional: Flash screen or show temporary success message */
            }
            break;

        default:
            /* Handle Hexadecimal Input */
            if (ed->size > 0) {
                int val = hex_char_to_val(ch);
                if (val != -1) {
                    if (ed->hex_state == 0) {
                        ed->temp_hex = (uint8_t)(val << 4);
                        ed->hex_state = 1;
                    } else {
                        ed->data[ed->cursor] = ed->temp_hex | (uint8_t)val;
                        ed->modified = 1;
                        ed->hex_state = 0;
                        if (ed->cursor + 1 < ed->size) ed->cursor++;
                    }
                } else {
                    /* Invalid hex character pressed, reset state */
                    ed->hex_state = 0; 
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
    
    if (load_file(&editor, argv[1]) != 0) {
        fprintf(stderr, "Error: Could not open or read file '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }

    /* Initialize ncurses */
    initscr();
    raw();
    keypad(stdscr, TRUE);
    noecho();
    curs_set(0); /* Hide default terminal cursor */

    /* Main Event Loop */
    while (1) {
        draw_screen(&editor);
        handle_input(&editor);
    }

    /* Cleanup (Unreachable in current loop, but good practice) */
    endwin();
    if (editor.data) free(editor.data);

    return EXIT_SUCCESS;
}
