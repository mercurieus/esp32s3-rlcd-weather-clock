#include "dither.h"

/* Values 0..15, tiled across the screen by (x % 4, y % 4). */
static const uint8_t BAYER_4X4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 },
};

uint8_t Dither_Threshold(int x, int y, uint8_t grey)
{
    /* Map the matrix cell to the centre of its slice of the 0..255 range,
       so grey=0 never paints anything and grey=255 always does. */
    const uint8_t cell = BAYER_4X4[y & 3][x & 3];
    const int threshold = (cell * 256 + 128) / 16;
    return (grey < threshold) ? 1 : 0;
}
