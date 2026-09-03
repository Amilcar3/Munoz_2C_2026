/*! @mainpage Ejercicio 3
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
 * | 13/08/2026 | Document creation		                         |
 *
 * @author Amilcar Muñoz (amilcar.munioz@gmail.com)
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include "led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/*==================[macros and definitions]=================================*/
#define LED_ON 1
#define LED_OFF 2
#define LED_TOGGLE 3
#define CONFIG_BLINK_PERIOD 1000
/*==================[internal data definition]===============================*/
typedef struct{
	uint8_t mode;			//			ON, OFF, TOGGLE 
	uint8_t n_led;			//		indica el numero de led a controlar
	uint8_t n_ciclos;		//		indica la cantidad de ciclos de prendido y apagado
	uint16_t periodo; 		//		indica el tiempo de cada ciclo
}leds_t;
/*==================[internal functions declaration]=========================*/
	
void ledsControl(leds_t *led);

/*==================[external functions definition]==========================*/
void ledsControl(leds_t *led){
	if (led->mode == LED_ON){
		switch(led->n_led){
			case 1:
				LedOn(LED_1);
				printf("LED 1 On\n");
				break;
			case 2:
				LedOn(LED_2);
				printf("LED 2 On\n");
				break;
			case 3:
				LedOn(LED_3);
				printf("LED 3 On\n");
				break;
			default:
				printf("Invalid LED number\n");
				break;
		}
		
	}
	else if (led->mode == LED_OFF){
		switch(led->n_led){
			case 1:
				LedOff(LED_1);
				printf("LED 1 Off\n");
				break;
			case 2:
				LedOff(LED_2);
				printf("LED 2 Off\n");
				break;
			case 3:
				LedOff(LED_3);
				printf("LED 3 Off\n");
				break;
			default:
				printf("Invalid LED number\n");
				break;
		}
		
	}
	else if (led->mode == LED_TOGGLE){
		for (int i = 0; i < led->n_ciclos; i++){
			
			switch(led->n_led){
				case 1:
					LedToggle(LED_1);
					printf("LED 1 Toggle\n");

					break;
				case 2:
					LedToggle(LED_2);
					printf("LED 2 Toggle\n");

					break;
				case 3:
					LedToggle(LED_3);
					printf("LED 3 Toggle\n");

					break;
				default:
					printf("Invalid LED number\n");
					break;
			}
			
			vTaskDelay(led->periodo / portTICK_PERIOD_MS);
			
		}

	}
}

void app_main(void){
	LedsInit();
	leds_t led1;
	leds_t *pled1 = &led1;
	led1.mode = LED_ON;
	led1.n_led = 1;	
	led1.n_ciclos = 10;
	led1.periodo = 500;
	ledsControl(pled1);
	leds_t led2;
	leds_t *pled2 = &led2;
	led2.mode = LED_ON;
	led2.n_led = 2;
	led2.n_ciclos = 10;
	led2.periodo = 500;
	ledsControl(pled2);
	leds_t led3;
	leds_t *pled3 = &led3;
	led3.mode = LED_TOGGLE;
	led3.n_led = 3;
	led3.n_ciclos = 10;
	led3.periodo = 500;
	ledsControl(pled3);

}

/*==================[end of file]============================================*/