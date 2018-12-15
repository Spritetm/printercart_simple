/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

//Select the type of printer cartridge by uncommenting the applicable define and commenting the other one.
//#define CART_IS_COLOR 1
#define CART_IS_BLACK 1

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>
#include "printcart_buffer_filler.h"
#include "printcart_genwaveform.h"
#include "printcart_i2s.h"
#include "read_picture.h"

//GPIO numbers for the lines that are connected (via level converters) to the printer cartridge.
#define PIN_NUM_CART_D2 12
#define PIN_NUM_CART_D1 27
#define PIN_NUM_CART_D3 13
#define PIN_NUM_CART_CSYNC 14
#define PIN_NUM_CART_S2 32
#define PIN_NUM_CART_S4 2
#define PIN_NUM_CART_S1 4
#define PIN_NUM_CART_S5 5
#define PIN_NUM_CART_DCLK 18
#define PIN_NUM_CART_S3 19
#define PIN_NUM_CART_F3 15
#define PIN_NUM_CART_F5 21

//GPIO number for the button. Note: this code assumes a button that pulls the GPIO low
//and  if this is >= 34, an external pull-up resistor is installed.
#define PIN_NUM_BTN 34

//Queue for nozzle data
QueueHandle_t nozdata_queue;


#define WAVEFORM_DMALEN 1500

void printcart_init() {
	//Create nozzle data queue
	nozdata_queue=xQueueCreate(1, PRINTCART_NOZDATA_SZ);
	//Initialize I2S parallel device. Use the function to generate waveform data from nozzle data as the callback
	//function.
	i2s_parallel_config_t i2scfg={
		.gpio_bus={
			PIN_NUM_CART_D1, //0
			PIN_NUM_CART_D2, //1
			PIN_NUM_CART_D3, //2
			PIN_NUM_CART_CSYNC, //3
			PIN_NUM_CART_S2, //4
			PIN_NUM_CART_S4, //5
			PIN_NUM_CART_S1, //6
			PIN_NUM_CART_S5, //7
			PIN_NUM_CART_DCLK, //8
			PIN_NUM_CART_S3, //9
			PIN_NUM_CART_F3, //10
			PIN_NUM_CART_F5, //11
			-1, -1, -1, -1 //12-15 - unused
		},
		.bits=I2S_PARALLEL_BITS_16,
		.clkspeed_hz=3333333, //3.3MHz
		.bufsz=WAVEFORM_DMALEN*sizeof(uint16_t),
		.refill_cb=printcart_buffer_filler_fn,
		.refill_cb_arg=nozdata_queue
	};
	i2s_parallel_setup(&I2S1, &i2scfg);
	i2s_parallel_start(&I2S1);
#ifdef CART_IS_COLOR
	printcart_select_waveform(PRINTCART_WAVEFORM_COLOR_B);
#endif
#ifdef CART_IS_BLACK
	printcart_select_waveform(PRINTCART_WAVEFORM_BLACK_B);
#endif
	//Done!
	printf("Printcart driver inited\n");
}


//In the color cartridge, there are three rows, one for each color. They're next to eachother, so we need to take care
//to grab the bits of image that actually are in the position of the nozzles.
#define CMY_ROW_OFFSET 16
void send_image_row_color(int pos) {
	uint8_t nozdata[PRINTCART_NOZDATA_SZ];
	memset(nozdata, 0, PRINTCART_NOZDATA_SZ);
	for (int c=0; c<3; c++) {
		for (int y=0; y<84; y++) {
			uint8_t v=picture_get_pixel(pos-c*CMY_ROW_OFFSET, y*2, c);
			//Note the v returned is 0 for black, 255 for the color. We need to invert that here as we're printing on
			//white.
			v=255-v;
			//Random-dither. The chance of the nozzle firing is equal to (v/256).
			if (v>(rand()&255)) {
				//Note: The actual nozzles for the color cart start around y=14
				printcart_fire_nozzle_color(nozdata, y+14, c);
			}
		}
	}
	//Send nozzle data to queue so ISR can pick up on it.
	xQueueSend(nozdata_queue, nozdata, portMAX_DELAY);
}

//In the mono cartridge, there are two rows of nozzles, slightly offset (in the X direction) from the other.
#define BLACK_ROW_OFFSET 10
void send_image_row_black(int pos) {
	uint8_t nozdata[PRINTCART_NOZDATA_SZ];
	memset(nozdata, 0, PRINTCART_NOZDATA_SZ);
	for (int row=0; row<2; row++) {
		for (int y=0; y<168; y++) {
			//We take anything but white in any color channel of the image to mean we want black there.
			if (picture_get_pixel(pos+row*BLACK_ROW_OFFSET, y, 0)!=0xff ||
				picture_get_pixel(pos+row*BLACK_ROW_OFFSET, y, 1)!=0xff ||
				picture_get_pixel(pos+row*BLACK_ROW_OFFSET, y, 2)!=0xff) {
				//Random-dither 50%, as firing all nozzles is a bit hard on the power supply.
				if (rand()&1) {
					printcart_fire_nozzle_black(nozdata, y, row);
				}
			}
		}
	}
	//Send nozzle data to queue so ISR can pick up on it.
	xQueueSend(nozdata_queue, nozdata, portMAX_DELAY);
}

//Returns true if button is pressed. Duh.
int button_pressed() {
	return (!gpio_get_level(PIN_NUM_BTN));
}

//Set up button GPIO
void button_init() {
	gpio_config_t io_conf={
		.mode=GPIO_MODE_INPUT,
		.pin_bit_mask=(1ULL<<PIN_NUM_BTN),
		.pull_up_en=1,
	};
	gpio_config(&io_conf);
}

void mainloop(void *arg) {
	while(1) {
		//Wait till user presses a button.
		printf("Waiting for button press...\n");
		while(!button_pressed()) {
			vTaskDelay(1);
		}
		printf("Dispensing ink!\n");
		int pos=0;
		while(button_pressed()) {
#ifdef CART_IS_COLOR
			send_image_row_color(pos/2);
#endif
#ifdef CART_IS_BLACK
			send_image_row_black(pos/2);
#endif
			pos++;
		}
	}
}

int app_main(void) {
	//Initialize button GPIO
	button_init();
	//Initialize printer cartridge driver and install the interrupt for it.
	printcart_init();
	//As the printcart interrupt is on core 0, better use core 1 for the image processing stuff that happens in the main loop.
	xTaskCreatePinnedToCore(mainloop, "mainloop", 1024*16, NULL, 7, NULL, 1);
	return 0;
}

