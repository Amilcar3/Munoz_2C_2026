/*! @mainpage Ejercicio 4
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
/*==================[macros and definitions]=================================*/

/*==================[internal data definition]===============================*/

/*==================[internal functions declaration]=========================*/
int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number);
/*==================[external functions definition]==========================*/
int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number){
	if (bcd_number == NULL) {
		return -1;
	}
	for (uint8_t i = 0; i < digits; i++){
		bcd_number[digits - 1 - i] = data % 10;
		data = data / 10;
	}

	return 0;
}
void app_main(void){

	uint32_t decimal_number = 157;
	uint8_t digits = 3;
	uint8_t bcd_number[digits];  

	int8_t error = convertToBcdArray(decimal_number, digits, bcd_number);
	
	if (error == 0) {
		for (uint8_t i = 0; i < digits; i++) {
			printf("%d \n", bcd_number[i]);
		}
		printf("\n");
	} else {
		printf("Error en la conversión.\n");
	}

}
/*==================[end of file]============================================*/