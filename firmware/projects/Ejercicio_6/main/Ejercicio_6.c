/*! @mainpage Ejercicio_6
 *
 * @section genDesc General Description
 *
 * Este programa muestra un número en un LCD de 3 dígitos usando codificación BCD.
 * Convierte un entero de 32 bits a BCD, envía cada dígito a GPIO_20..23
 * (líneas de datos D1-D4), y latchea cada dígito usando GPIO_19, GPIO_18
 * y GPIO_9 (líneas de selección SEL_1, SEL_2, SEL_3).
 *
 * <a href="https://drive.google.com/...">Operation Example</a>
 *
 * @section hardConn Hardware Connection
 *
 * |    Peripheral  |   ESP32   	|
 * |:--------------:|:--------------|
 * | 	D1	 	    | 	GPIO_20		|
 * | 	D2	 	    | 	GPIO_21		|
 * | 	D3	 	    | 	GPIO_22		|
 * | 	D4	 	    | 	GPIO_23		|
 * | 	SEL_1	 	| 	GPIO_19		|
 * | 	SEL_2	 	| 	GPIO_18		|
 * | 	SEL_3	 	| 	GPIO_9		|
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

/*==================[internal data definition]===============================*/

/** @brief Estructura de configuración de GPIO para pin y dirección. */
typedef struct{
    gpio_t pin;     /*!< Número de pin GPIO */
    io_t dir;       /*!< Dirección del GPIO '0' IN; '1' OUT */
} gpioConf_t;

/** @brief Vector de GPIOs para las líneas de datos BCD (D1-D4). */
gpioConf_t gpio_bcd[4] = {
    {GPIO_20, GPIO_OUTPUT},
    {GPIO_21, GPIO_OUTPUT},
    {GPIO_22, GPIO_OUTPUT},
    {GPIO_23, GPIO_OUTPUT}
};

/** @brief Vector de GPIOs para la selección de dígito del display (SEL_1, SEL_2, SEL_3). */
gpioConf_t gpio_display[3] = {
    {GPIO_19, GPIO_OUTPUT},
    {GPIO_18, GPIO_OUTPUT},
    {GPIO_9,  GPIO_OUTPUT}
};

/*==================[internal functions declaration]=========================*/

int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number);
void gpioChangeState(uint8_t bcd_digit, gpioConf_t *vector);
void displayShowBcd(uint32_t data, uint8_t digits, gpioConf_t *vector_bcd, gpioConf_t *vector_display);

/*==================[external functions definition]==========================*/

/**
 * @brief Convierte un entero de 32 bits a un arreglo BCD.
 *
 * Separa cada dígito decimal del dato de entrada y los almacena
 * en el arreglo de salida, con el dígito más significativo primero.
 *
 * @param data       Número de 32 bits a convertir.
 * @param digits     Cantidad de dígitos BCD a extraer.
 * @param bcd_number Puntero al arreglo de salida donde se almacenan los dígitos BCD.
 * @return int8_t    0 si fue exitoso, -1 si bcd_number es NULL.
 */
int8_t convertToBcdArray(uint32_t data, uint8_t digits, uint8_t *bcd_number)
{
    if (bcd_number == NULL) {
        return -1;
    }
    for (uint8_t i = 0; i < digits; i++) {
        bcd_number[digits - 1 - i] = data % 10;
        data = data / 10;
    }
    return 0;
}

/**
 * @brief Configura los pines GPIO según los bits de un dígito BCD.
 *
 * Recorre los 4 bits del dígito BCD y setea cada pin GPIO
 * correspondiente en ON u OFF.
 *
 * @param bcd_digit Dígito BCD (0-9) cuyos bits controlan los GPIOs.
 * @param vector    Puntero a un arreglo de 4 elementos gpioConf_t que mapea bits a pines.
 */
void gpioChangeState(uint8_t bcd_digit, gpioConf_t *vector)
{
    for (uint8_t i = 0; i < 4; i++)
    {
        if ((bcd_digit >> i) & 0x01) {
            GPIOOn(vector[i].pin);
        } else {
            GPIOOff(vector[i].pin);
        }
    }
}

/**
 * @brief Muestra un número en un LCD multiplexado usando BCD.
 *
 * Convierte el dato de entrada a BCD, luego para cada dígito setea las
 * líneas de datos BCD y genera un pulso en el pin de selección correspondiente
 * para latchear el valor en el decodificador CD4543.
 *
 * @param data           Número de 32 bits a mostrar.
 * @param digits         Cantidad de dígitos a mostrar (máx. 3).
 * @param vector_bcd     Puntero al arreglo gpioConf_t de líneas de datos BCD (D1-D4).
 * @param vector_display Puntero al arreglo gpioConf_t de selección de dígito (SEL_1-SEL_3).
 */
void displayShowBcd(uint32_t data, uint8_t digits, gpioConf_t *vector_bcd, gpioConf_t *vector_display)
{
    uint8_t bcd_number[digits];

    convertToBcdArray(data, digits, bcd_number);

    for (uint8_t i = 0; i < digits; i++)
    {
        gpioChangeState(bcd_number[i], vector_bcd);
        GPIOOn(vector_display[i].pin);
        GPIOOff(vector_display[i].pin);
    }
}

/**
 * @brief Función principal de la aplicación.
 *
 * Inicializa los GPIOs para datos BCD y selección de display,
 * luego muestra el número 123 en el LCD de 3 dígitos.
 */
void app_main(void)
{
    for (uint8_t i = 0; i < 4; i++) {
        GPIOInit(gpio_bcd[i].pin, gpio_bcd[i].dir);
    }
    for (uint8_t i = 0; i < 3; i++) {
        GPIOInit(gpio_display[i].pin, gpio_display[i].dir);
    }

    displayShowBcd(123, 3, gpio_bcd, gpio_display);
}
/*==================[end of file]============================================*/