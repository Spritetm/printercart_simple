#include <stdint.h>

#pragma once

//Returns a pixel from the selected channel of the selected coordinate of an embedded image.
//Returns white when out of bounds in the Y direction; image wraps around in the X direction.
//Color=0 returns red, color=1 returns green, color=3 returns the blue pixel data (0-255).
uint8_t picture_get_pixel(int x, int y, int color);
