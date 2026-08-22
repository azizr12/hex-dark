#ifndef PLUGIN_H
#define PLUGIN_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    HIGHLIGHT_NORMAL = 0,
    HIGHLIGHT_JPEG_SOI,      // FF D8 (Start of Image)
    HIGHLIGHT_JPEG_EOI,      // FF D9 (End of Image)
    HIGHLIGHT_JPEG_MARKER,   // FF xx (Segment headers like APP0, DQT, SOF, SOS)
    HIGHLIGHT_JPEG_DATA      // Payload data between markers (optional for future use)
} HighlightCategory;

/* 
 * Categorizes a byte for syntax highlighting.
 * Pass the current window buffer, the offset within that buffer, and the buffer length.
 */
HighlightCategory get_highlight_category(const uint8_t *buffer, size_t offset, size_t buffer_len);

/* Example translation function */
uint8_t translate_byte(uint8_t b);

#endif /* PLUGIN_H */