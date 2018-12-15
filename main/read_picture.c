#include <stdint.h>
#include <stdio.h>

//Note: Make sure the picture actually has this size...
#define PICTURE_W 810
#define PICTURE_H 164

//Picture data.
extern const uint8_t picture_start[]   asm("_binary_picture_rgb_start");
extern const uint8_t picture_end[]     asm("_binary_picture_rgb_end");

//Note that this function wraps X around, so the text repeats.
uint8_t picture_get_pixel(int x, int y, int color) {
	x=x%PICTURE_W;
	if (x<0) x+=PICTURE_W;
	if (y<0 || y>=PICTURE_H) return 0xFF;
	const uint8_t *p=&picture_start[(y*PICTURE_W+x)*3];
	return p[color];
//	return (x==y)?0:0xff;
}

