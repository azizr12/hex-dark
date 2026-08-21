#include <stdint.h>

// Example Plugin: Converts standard ASCII to a custom "Dark Mode" visual mapping 
// or simply applies ROT13 to letters to demonstrate translation.
uint8_t translate_byte(uint8_t b) {
    if (b >= 'a' && b <= 'z') return 'a' + (b - 'a' + 13) % 26;
    if (b >= 'A' && b <= 'Z') return 'A' + (b - 'A' + 13) % 26;
    return b; // Pass through non-letters
}
