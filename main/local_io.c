/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */


/*
This contains initialization for any hardware on the board that's not strictly to do
with the ink cartridge interface. The hardware I built needs to toggle a pin on an
I2C expander to turn on the HV for the cart, we do that here. Put whatever your board
need here instead if it's different.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2c.h"

#define PIN_NUM_SDA 23
#define PIN_NUM_SCL 22

#define TCA_XSHUT3 (1<<0)
#define TCA_XSHUT1 (1<<1)
#define TCA_BOOSTEN (1<<2)
#define TCA_WHLED (1<<3)
#define TCA_LCDRST (1<<4)
#define TCA_BKLT (1<<5)
#define TCA_XSHUT2 (1<<7)

#define I2C_PORT 0

#define TCA_REG_INPUT 0
#define TCA_REG_OUTPUT 1
#define TCA_REG_POLINV 2
#define TCA_REG_CONF 3

#define TCA_ADR 0x20  //Address for a TCA9534
#define TCA_A_ADR 0x38  //Address this for a TCA9534A


void local_io_init() {
	i2c_config_t conf = {
		.mode = I2C_MODE_MASTER,
		.sda_io_num = PIN_NUM_SDA,
		.sda_pullup_en = GPIO_PULLUP_ENABLE,
		.scl_io_num = PIN_NUM_SCL,
		.scl_pullup_en = GPIO_PULLUP_ENABLE,
		.master.clk_speed = 10*1000,
	};
	ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
	ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));

	//Autodetect -A variant of RCA9534
	int adr=TCA_ADR;
	uint8_t w[2];
	if (i2c_master_write_to_device(I2C_PORT, adr, w, 0, pdMS_TO_TICKS(100))!=ESP_OK) adr=TCA_A_ADR;

	w[0]=TCA_REG_CONF;
	w[1]=0; //all output
	ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_PORT, adr, w, 2, pdMS_TO_TICKS(100)));
	w[0]=TCA_REG_OUTPUT;
	w[1]=TCA_BOOSTEN;
	ESP_ERROR_CHECK(i2c_master_write_to_device(I2C_PORT, adr, w, 2, pdMS_TO_TICKS(100)));
}




