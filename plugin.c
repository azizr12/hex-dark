#include "plugin.h"

/* ================================================================== */
/* JPEG Structure Highlighting Logic                                  */
/* ================================================================== */

HighlightCategory get_highlight_category(const uint8_t *buffer, size_t offset, size_t buffer_len) {
    if (!buffer || offset >= buffer_len) {
        return HIGHLIGHT_NORMAL;
    }

    uint8_t current = buffer[offset];
    uint8_t next    = (offset + 1 < buffer_len) ? buffer[offset + 1] : 0x00;
    uint8_t prev    = (offset > 0)            ? buffer[offset - 1] : 0x00;

    // 1. JPEG Start of Image (SOI): FF D8
    if (current == 0xFF && next == 0xD8) {
        return HIGHLIGHT_JPEG_SOI;
    }

    // 2. JPEG End of Image (EOI): FF D9
    if (current == 0xFF && next == 0xD9) {
        return HIGHLIGHT_JPEG_EOI;
    }

    // 3. JPEG Markers: FF xx (where xx != 00, D8, D9)
    // Note: FF 00 is an escaped FF in JPEG compressed data, NOT a marker.
    if (current == 0xFF && next != 0x00 && next != 0xD8 && next != 0xD9) {
        return HIGHLIGHT_JPEG_MARKER;
    }

    // 4. The second byte of a marker pair (e.g., the 'D8' in 'FF D8')
    if (prev == 0xFF && current != 0x00) {
        if (current == 0xD8) return HIGHLIGHT_JPEG_SOI;
        if (current == 0xD9) return HIGHLIGHT_JPEG_EOI;
        return HIGHLIGHT_JPEG_MARKER;
    }

    // 5. Default: Normal data byte
    return HIGHLIGHT_NORMAL;
}

/* ================================================================== */
/* Example Plugin: ROT13 Translation                                  */
/* ================================================================== */

uint8_t translate_byte(uint8_t b) {
    if (b >= 'a' && b <= 'z') return 'a' + (b - 'a' + 13) % 26;
    if (b >= 'A' && b <= 'Z') return 'A' + (b - 'A' + 13) % 26;
    return b; // Pass through non-letters
}