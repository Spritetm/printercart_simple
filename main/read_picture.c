//Simple routine to read bytes from an embedded raw rgb file

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

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
	//Nozzles are bottom-first, picture is top-first. Compensate.
	y=PICTURE_H-y-1;
	const uint8_t *p=&picture_start[(y*PICTURE_W+x)*3];
	return p[color];
}

