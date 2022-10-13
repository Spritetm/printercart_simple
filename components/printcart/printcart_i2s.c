//Quick-and-dirty implementation of a driver for the parallel output mode of the
//ESP32 I2S peripheral. Note this is only tested on 16-bit parallel output at this
//moment.

/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "soc/i2s_struct.h"
#include "soc/i2s_reg.h"
#include "driver/periph_ctrl.h"
#include "soc/io_mux_reg.h"
#include "esp32/rom/lldesc.h"
#include "esp_heap_caps.h"
#include "soc/gpio_sig_map.h"
#include "esp_intr_alloc.h"
#include "driver/gpio.h"
#include "soc/gpio_periph.h"
#include "rom/gpio.h"
#include "printcart_i2s.h"
#include "esp_attr.h"
#include "printcart_genwaveform.h"

typedef struct {
	volatile lldesc_t *dmadesc_a, *dmadesc_b;
	int desccount_a, desccount_b;
	i2s_parallel_refill_buffer_cb_t refill_cb;
	void *refill_cb_arg;
	int bufsz;
} i2s_parallel_state_t;

static i2s_parallel_state_t *i2s_state[2]={NULL, NULL};

static void i2s_int_hdl(void *arg);

#define DMA_MAX (4096-4)

//Calculate the amount of dma descs needed for a certain memory size
static int calc_needed_dma_descs_for(int memsize) {
	return (memsize+DMA_MAX-1)/DMA_MAX;
}

//Fill in the data structures for previously allocated DMA descriptors so they cover all
//memory that is passed as the buffer descriptors.
static int fill_dma_desc(volatile lldesc_t *dmadesc, void *memory, int size) {
	int n=0;
	int len=size;
	uint8_t *data=((uint8_t*)memory);
	while(len) {
		int dmalen=len;
		if (dmalen>DMA_MAX) dmalen=DMA_MAX;
		dmadesc[n].size=dmalen;
		dmadesc[n].length=dmalen;
		dmadesc[n].buf=data;
		dmadesc[n].eof=0;
		dmadesc[n].sosf=0;
		dmadesc[n].owner=1;
		dmadesc[n].qe.stqe_next=(lldesc_t*)&dmadesc[n+1];
		dmadesc[n].offset=0;
		len-=dmalen;
		data+=dmalen;
		n++;
	}
	return n;
}

//Generic routine to set a GPIO to an I2S signal
static void gpio_setup_out(int gpio, int sig) {
	if (gpio==-1) return;
	PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpio], PIN_FUNC_GPIO);
	gpio_set_direction(gpio, GPIO_MODE_DEF_OUTPUT);
	gpio_matrix_out(gpio, sig, false, false);
}


static void dma_reset(volatile i2s_dev_t *dev) {
	dev->lc_conf.in_rst=1; dev->lc_conf.in_rst=0;
	dev->lc_conf.out_rst=1; dev->lc_conf.out_rst=0;
}

static void fifo_reset(volatile i2s_dev_t *dev) {
	dev->conf.rx_fifo_reset=1; dev->conf.rx_fifo_reset=0;
	dev->conf.tx_fifo_reset=1; dev->conf.tx_fifo_reset=0;
}

static int i2snum(volatile i2s_dev_t *dev) {
	return (dev==&I2S0)?0:1;
}

void i2s_parallel_setup(volatile i2s_dev_t *dev, const i2s_parallel_config_t *cfg) {
	printf("Setting up parallel I2S bus at I2S%d\n", i2snum(dev));
	int sig_data_base;

	//Power on peripheral
	if (dev==&I2S0) {
		periph_module_enable(PERIPH_I2S0_MODULE);
	} else {
		periph_module_enable(PERIPH_I2S1_MODULE);
	}

	//Route the signals from the selected I2S bus to the GPIOs
	//Because of... reasons... the 16-bit values appear on d8...d23
	if (dev==&I2S0) {
		if (cfg->bits==I2S_PARALLEL_BITS_32) {
			sig_data_base=I2S0O_DATA_OUT0_IDX;
		} else {
			sig_data_base=I2S0O_DATA_OUT8_IDX;
		}
	} else {
		if (cfg->bits==I2S_PARALLEL_BITS_32) {
			sig_data_base=I2S1O_DATA_OUT0_IDX;
		} else {
			sig_data_base=I2S1O_DATA_OUT8_IDX;
		}
	}
	for (int x=0; x<cfg->bits; x++) {
		gpio_setup_out(cfg->gpio_bus[x], sig_data_base+x);
	}
	
	//Initialize I2S dev
	fifo_reset(dev);
	dma_reset(dev);
	dev->conf.rx_reset=1; dev->conf.tx_reset=1;
	dev->conf.rx_reset=0; dev->conf.tx_reset=0;
	
	//Enable LCD mode (=parallel output mode)
	dev->conf2.val=0;
	dev->conf2.lcd_en=1;
	
	dev->sample_rate_conf.val=0;
	dev->sample_rate_conf.rx_bits_mod=cfg->bits; //don't really matter
	dev->sample_rate_conf.tx_bits_mod=cfg->bits;
	dev->sample_rate_conf.rx_bck_div_num=40000000/cfg->clkspeed_hz;  //Divider. 40MHz/x
	dev->sample_rate_conf.tx_bck_div_num=40000000/cfg->clkspeed_hz;
	
	dev->clkm_conf.val=0;
	dev->clkm_conf.clka_en=0;
	dev->clkm_conf.clkm_div_a=0;
	dev->clkm_conf.clkm_div_b=0;
	//We ignore the possibility for fractional division here.
	dev->clkm_conf.clkm_div_num=1;
	
	dev->fifo_conf.val=0;
	dev->fifo_conf.rx_fifo_mod_force_en=1;
	dev->fifo_conf.tx_fifo_mod_force_en=1;
	dev->fifo_conf.tx_fifo_mod=1;
	dev->fifo_conf.tx_fifo_mod=1;
	dev->fifo_conf.rx_data_num=32; //Thresholds. 
	dev->fifo_conf.tx_data_num=32;
	dev->fifo_conf.dscr_en=1;
	
	dev->conf1.val=0;
	dev->conf1.tx_stop_en=1;
	dev->conf1.tx_pcm_bypass=1;
	
	dev->conf_chan.val=0;
	dev->conf_chan.tx_chan_mod=1;
	dev->conf_chan.rx_chan_mod=1;
	
	//Invert ws to be active-low... ToDo: make this configurable
	dev->conf.tx_right_first=1;
	dev->conf.rx_right_first=1;
	
	dev->timing.val=0;
	
	//Allocate memory
	void *mema=heap_caps_malloc(cfg->bufsz, MALLOC_CAP_DMA);
	void *memb=heap_caps_malloc(cfg->bufsz, MALLOC_CAP_DMA);
	memset(mema, 0, cfg->bufsz);
	memset(memb, 0, cfg->bufsz);

	//Allocate DMA descriptors
	i2s_state[i2snum(dev)]=malloc(sizeof(i2s_parallel_state_t));
	i2s_parallel_state_t *st=i2s_state[i2snum(dev)];
	st->desccount_a=calc_needed_dma_descs_for(cfg->bufsz);
	st->desccount_b=calc_needed_dma_descs_for(cfg->bufsz);
	st->dmadesc_a=heap_caps_malloc(st->desccount_a*sizeof(lldesc_t), MALLOC_CAP_DMA);
	st->dmadesc_b=heap_caps_malloc(st->desccount_b*sizeof(lldesc_t), MALLOC_CAP_DMA);
	
	//and fill them
	int cta=fill_dma_desc(st->dmadesc_a, mema, cfg->bufsz);
	int ctb=fill_dma_desc(st->dmadesc_b, memb, cfg->bufsz);
	//Make last dma desc generate EOF and link back to first of other buffer
	st->dmadesc_a[cta-1].eof=1;
	st->dmadesc_b[ctb-1].eof=1;
	st->dmadesc_a[cta-1].qe.stqe_next=(lldesc_t*)&st->dmadesc_b[0];
	st->dmadesc_b[ctb-1].qe.stqe_next=(lldesc_t*)&st->dmadesc_a[0];

	//Set int handler
	esp_intr_alloc(ETS_I2S0_INTR_SOURCE + i2snum(dev), 0, i2s_int_hdl, (void*)dev, NULL);

	//Save buffer fill callback
	st->refill_cb=cfg->refill_cb;
	st->refill_cb_arg=cfg->refill_cb_arg;
	st->bufsz=cfg->bufsz;

	//Note: call i2s_parallel_start to start transmission.
}

//ISR handler. Call callback to refill buffer that was just finished.
static void IRAM_ATTR i2s_int_hdl(void *arg) {
	volatile i2s_dev_t* dev = arg;
	int devno=i2snum(dev);
	if (dev->int_st.out_eof) {
	dev->int_clr.val = dev->int_st.val;
		lldesc_t *finish_desc = (lldesc_t*)dev->out_eof_des_addr;
		i2s_state[devno]->refill_cb((void*)finish_desc->buf, i2s_state[devno]->bufsz, i2s_state[devno]->refill_cb_arg);
	}
}

void i2s_parallel_start(volatile i2s_dev_t *dev) {
	int devno=i2snum(dev);
	//Prefill buffers using callback.
	i2s_state[devno]->refill_cb((void*)i2s_state[devno]->dmadesc_a[0].buf, i2s_state[devno]->bufsz, i2s_state[devno]->refill_cb_arg);
	i2s_state[devno]->refill_cb((void*)i2s_state[devno]->dmadesc_b[0].buf, i2s_state[devno]->bufsz, i2s_state[devno]->refill_cb_arg);

   //Reset FIFO/DMA -> needed? Doesn't dma_reset/fifo_reset do this?
	dev->out_link.stop=1;
	dev->out_link.start=0;
	dev->conf.tx_start=0;
	vTaskDelay(2);
	dma_reset(dev); fifo_reset(dev);
	dev->lc_conf.in_rst=1; dev->lc_conf.out_rst=1; dev->lc_conf.ahbm_rst=1; dev->lc_conf.ahbm_fifo_rst=1;
	dev->lc_conf.in_rst=0; dev->lc_conf.out_rst=0; dev->lc_conf.ahbm_rst=0; dev->lc_conf.ahbm_fifo_rst=0;
	dev->conf.tx_reset=1; dev->conf.tx_fifo_reset=1; dev->conf.rx_fifo_reset=1;
	dev->conf.tx_reset=0; dev->conf.tx_fifo_reset=0; dev->conf.rx_fifo_reset=0;

	fifo_reset(dev);
	dma_reset(dev);
	dev->conf.rx_reset=1; dev->conf.tx_reset=1;
	dev->conf.rx_reset=0; dev->conf.tx_reset=0;

	vTaskDelay(5);
	dev->lc_conf.val=I2S_OUT_DATA_BURST_EN | I2S_OUTDSCR_BURST_EN | I2S_OUT_DATA_BURST_EN;
	dev->out_link.addr=((uint32_t)(&i2s_state[i2snum(dev)]->dmadesc_a[0]));
	vTaskDelay(5);
	dev->out_link.start=1;
	vTaskDelay(5);
	dev->conf.tx_start=1;
	dev->int_ena.out_eof = 1;
}

