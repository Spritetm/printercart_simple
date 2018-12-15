#include <stdint.h>

#pragma once

//For debugging: the amount of buffer memory the printcart_buffer_filler_fn function actually used
extern int printcart_mem_words_used;

enum printcart_buffer_filler_waveform_type_en {
	PRINTCART_WAVEFORM_COLOR_A=0,		//old, duplicates lines on 2nd color cart
	PRINTCART_WAVEFORM_COLOR_B,			//works on 2nd color cart
	PRINTCART_WAVEFORM_BLACK_A,			//old, works on bw cart
	PRINTCART_WAVEFORM_BLACK_B,			//new bw cart
};

//Select one of the above waveforms to be used.
void printcart_select_waveform(enum printcart_buffer_filler_waveform_type_en waveform_id);

/*
Meant as a callback from the I2S buffer filling code. Fills a buffer with the data from the queue passed as the argument,
or a 'blank' pattern if the buffer is empty.
*/
void printcart_buffer_filler_fn(void *buf, int len, void *arg);
