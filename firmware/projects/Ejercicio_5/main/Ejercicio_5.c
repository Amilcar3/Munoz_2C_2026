/*! @mainpage Ejercicio_5
 *
 * @section genDesc General Description
 *
 * This section describes how the program works.
 *
 * <a href="https://drive.google.com/...">Operation Example</a>
 *
 * @section hardConn Hardware Connection
 *
 * |    Peripheral  |   ESP32   	|
 * |:--------------:|:--------------|
 * | 	PIN_X	 	| 	GPIO_X		|
 *
 *
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 01/09/2026 | Document creation		                         |
 *
 * @author Amilcar Muñoz (amilcar.munioz@gmail.com)
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include "gpio_mcu.h"
/*==================[macros and definitions]=================================*/

/*==================[internal data definition]===============================*/

typedef struct{
	gpio_t pin; 	/*!< GPIO pin number */
	io_t dir; 		/*!< GPIO direction '0' IN; '1' OUT*/
} gpioConf_t;

gpioConf_t gpio_bcd[4] = {
    {GPIO_20, GPIO_OUTPUT},
    {GPIO_21, GPIO_OUTPUT},
    {GPIO_22, GPIO_OUTPUT},
    {GPIO_23, GPIO_OUTPUT}
};

/*==================[internal functions declaration]=========================*/

void gpioChangeState(uint8_t bcd_digit, gpioConf_t *vector);

/*==================[external functions definition]==========================*/


void gpioChangeState(uint8_t bcd_digit, gpioConf_t *vector)
{
    for (uint8_t i = 0; i < 4; i++)
    {
        if ((bcd_digit >> i) & 0x01)
        {
            GPIOOn(vector[i].pin);
        }
        else
        {
            GPIOOff(vector[i].pin);
        }
    }
}

void app_main(void){

	for (uint8_t i = 0; i < 4; i++)
    {
        GPIOInit(gpio_bcd[i].pin, gpio_bcd[i].dir);
    }

    gpioChangeState(5, gpio_bcd);
    printf("BCD digit 5 sent to GPIO_20..GPIO_23\n");
}
/*==================[end of file]============================================*/