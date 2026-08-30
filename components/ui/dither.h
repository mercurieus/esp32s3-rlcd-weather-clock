#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ordered (Bayer 4x4) dithering: given a target grey level (0 = black,
   255 = white) and a screen position, decides whether this one pixel
   should be black or white. Tiling the 4x4 matrix across a region turns a
   flat grey fill into a checker-like pattern that approximates that grey
   on a 1-bit panel - a plain grey fill would just collapse to solid under
   this panel's hard RGB565-at-0x7fff threshold (see Lvgl_FlushCallback in
   main/main.cpp), so the dithering has to happen at this level, before
   LVGL or the panel driver ever sees a single flat colour. */
uint8_t Dither_Threshold(int x, int y, uint8_t grey);

#ifdef __cplusplus
}
#endif
