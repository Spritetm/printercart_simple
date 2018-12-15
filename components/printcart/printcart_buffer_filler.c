//Implementation of the the I2S buffer fill callback function that grabs new nozzle data from a
//FreeRTOS queue and uses the printcart_genwaveform code to convert that into a printer
//cart waveform.

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_attr.h"
#include "printcart_buffer_filler.h"
#include "printcart_genwaveform.h"
#include <string.h>

//Debugging variable. Declare 'extern' somewhere else and print to find out the needed minimum size of 
//the i2s buffers.
int printcart_mem_words_used=0;

//To decode or change these waveforms, please use tools/waveform_editor.html
static uint16_t waveform_tpl_color_a[]={
	0x0,0xc001,0xc101,0xc142,0x42,0x4001,0x4101,0x4112,0x12,0x4001,0x4101,0x4302,0x602,0x4401,0x4501,0x4522,0x422,0x402,0x4000,0x4000,0x4080,0x880,0x800,0x800,0x800,0x800,0x800
};
static uint16_t waveform_tpl_color_b[]={
	0x0,0xc000,0xc101,0xc141,0x42,0x2,0x4101,0x4111,0x12,0x4002,0x4101,0x4301,0x602,0x4402,0x4501,0x4521,0x422,0x402,0x4000,0x4000,0x4080,0x880,0x800,0x800,0x800,0x800,0x800
};
static uint16_t waveform_tpl_black_a[]={
	0x0,0xc001,0xc101,0xc142,0x42,0xc001,0xc101,0xc112,0x12,0x4001,0x4101,0x4302,0x602,0x401,0x501,0x522,0x422,0x402,0x4000,0x4000,0x4080,0x880,0x800,0x800,0x800,0x800,0x800
};
static uint16_t waveform_tpl_black_b[]={
	0x0,0xc000,0xc101,0xc141,0x42,0x2,0xc101,0xc111,0x12,0x4002,0x4101,0x4301,0x602,0x402,0x501,0x521,0x422,0x402,0x4002,0x4000,0x4080,0x880,0x800,0x800,0x800,0x800,0x800
};

/*
Structure to store the waveform data, so you can easily add others if needed.
*/
typedef struct {
	uint16_t *data;
	int len;
} waveform_desc_t;

const waveform_desc_t waveforms[]={
	{waveform_tpl_color_a, sizeof(waveform_tpl_color_a)/2},
	{waveform_tpl_color_b, sizeof(waveform_tpl_color_b)/2},
	{waveform_tpl_black_a, sizeof(waveform_tpl_black_a)/2},
	{waveform_tpl_black_b, sizeof(waveform_tpl_black_b)/2},
};

//Currently selected waveform and its length
static uint16_t *selected_waveform_tpl=waveform_tpl_color_a;
static int selected_waveform_len=sizeof(waveform_tpl_color_a)/2;

//If we switch waveforms, the buffer needs to be cleared. This being nonzero indicates this.
int clear_waveform_ct=0;

//Use this to select a different waveform.
void printcart_select_waveform(enum printcart_buffer_filler_waveform_type_en waveform_id) {
	clear_waveform_ct=3;
	selected_waveform_tpl=waveforms[waveform_id].data;
	selected_waveform_len=waveforms[waveform_id].len;
}

//This gets called from the I2S interrupt. Grab the next color data from the queue
//and fill the buffer with the corresponding waveform.
void IRAM_ATTR printcart_buffer_filler_fn(void *buf, int len, void *arg) {
	QueueHandle_t pixq=(QueueHandle_t)arg;
	portBASE_TYPE high_priority_task_awoken = 0;
		//Pre-clear waveform buffer, but only when needed.
	if (clear_waveform_ct) {
		clear_waveform_ct--;
		memset(buf, 0, len);
	}
	//Fix memory to receive the nozzle color data in
	uint8_t nozdata[PRINTCART_NOZDATA_SZ];
	//Receive from queue.
	if (xQueueReceiveFromISR(pixq, nozdata, &high_priority_task_awoken)==pdFALSE) {
		//Nothing in queue. Zero out data so we don't fire any nozzles.
		memset(nozdata, 0, PRINTCART_NOZDATA_SZ);
	}
	//Generate waveform
	printcart_mem_words_used=printcart_generate_waveform((uint16_t*)buf, selected_waveform_tpl, nozdata, selected_waveform_len);
	//Wake thread blocking on pixqueue
	if (high_priority_task_awoken == pdTRUE) {
		portYIELD_FROM_ISR();
	}
}
