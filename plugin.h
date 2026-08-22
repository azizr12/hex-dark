#include "plugin.h"

HighlightCategory get_highlight_category(const uint8_t *buffer, size_t offset, size_t buffer_len) {
    if (!buffer || offset >= buffer_len) return HIGHLIGHT_NORMAL;

    uint8_t current = buffer[offset];
    uint8_t next    = (offset + 1 < buffer_len) ? buffer[offset + 1] : 0x00;
    uint8_t prev    = (offset > 0)            ? buffer[offset - 1] : 0x00;

    if (current == 0xFF && next == 0xD8) return HIGHLIGHT_JPEG_SOI;
    if (current == 0xFF && next == 0xD9) return HIGHLIGHT_JPEG_EOI;
    if (current == 0xFF && next != 0x00 && next != 0xD8 && next != 0xD9) return HIGHLIGHT_JPEG_MARKER;
    
    if (prev == 0xFF && current != 0x00) {
        if (current == 0xD8) return HIGHLIGHT_JPEG_SOI;
        if (current == 0xD9) return HIGHLIGHT_JPEG_EOI;
        return HIGHLIGHT_JPEG_MARKER;
    }

    return HIGHLIGHT_NORMAL;
}

uint8_t translate_byte(uint8_t b) {
    if (b >= 'a' && b <= 'z') return 'a' + (b - 'a' + 13) % 26;
    if (b >= 'A' && b <= 'Z') return 'A' + (b - 'A' + 13) % 26;
    return b;
}