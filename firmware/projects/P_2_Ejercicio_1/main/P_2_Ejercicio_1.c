/*! @mainpage P_2_Ejercicio_1
 *
 * @section genDesc General Description
 *
 * Regla ultrasónica con display LCD sobre la placa EDU-ESP (ESP32-C6).
 * La aplicación está implementada con tres tareas de FreeRTOS que se
 * comunican mediante variables globales compartidas:
 *
 * - @ref measureTask : mide la distancia con el sensor HC-SR04 cada 1 s.
 * - @ref displayTask : indica la distancia con los LEDs y la muestra en el display LCD.
 * - @ref buttonTask  : lee las teclas por polling y modifica los indicadores de estado.
 *
 * Funcionalidad:
 * - TEC1 activa/detiene la medición.
 * - TEC2 activa/desactiva el modo HOLD (congela el valor mostrado en el LCD).
 * - LEDs según la distancia: < 10 cm todos apagados, 10-20 cm LED_1,
 *   20-30 cm LED_1 y LED_2, > 30 cm LED_1, LED_2 y LED_3.
 *
 * <a href="https://drive.google.com/...">Operation Example</a>
 *
 * @section hardConn Hardware Connection
 *
 * |    Peripheral  |   ESP32   	|
 * |:--------------:|:--------------|
 * | 	HC-SR04 ECHO	| 	GPIO_3		|
 * | 	HC-SR04 TRIGGER	| 	GPIO_2		|
 * | 	HC-SR04 +5V	| 	+5V		|
 * | 	HC-SR04 GND	| 	GND		|
 * | 	LCD D1-D4 (BCD)	| 	GPIO_20, GPIO_21, GPIO_22, GPIO_23	|
 * | 	LCD SEL_1-SEL_3	| 	GPIO_19, GPIO_18, GPIO_9	|
 * | 	TEC1 / TEC2	| 	GPIO_4 / GPIO_15 (onboard)	|
 * | 	LED_1-LED_3	| 	onboard	|
 *
 * @section flowApp Diagrama de flujo general
 *
 *
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 03/09/2026 | Document creation		                         |
 *
 * @author Amilcar Muñoz (amilcar.munioz@gmail.com)
 *
 */

/**
 * @file
 * @brief Regla ultrasónica con display LCD (Proyecto 2, Actividad 1).
 */

/*==================[inclusions]=============================================*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "gpio_mcu.h"
#include "hc_sr04.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "switch.h"
#include "lcditse0803.h"
/*==================[macros and definitions]=================================*/

/** @brief Período de la tarea de medición, en ms. */
#define CONFIG_TASK_MEASURE_PERIOD 1000
/** @brief Período de la tarea de visualización (LEDs y LCD), en ms. */
#define CONFIG_TASK_DISPLAY_PERIOD 1000
/** @brief Período de lectura de las teclas, en ms. */
#define CONFIG_TASK_BUTTON_PERIOD 250


/*==================[internal data definition]===============================*/

/** @brief Handle de la tarea de medición. */
TaskHandle_t measure_task_handle = NULL;
/** @brief Handle de la tarea de visualización. */
TaskHandle_t display_task_handle = NULL;
/** @brief Handle de la tarea de lectura de teclas. */
TaskHandle_t button_task_handle = NULL;
/** @brief Última distancia medida por el HC-SR04, en cm. */
uint16_t distance = 0;
/** @brief Indicador de medición activa (alternado por TEC1). */
bool button_1 = false;
/** @brief Indicador de modo HOLD activo (alternado por TEC2). */
bool button_2 = false;
/** @brief Máscara de teclas devuelta por SwitchesRead(). */
uint8_t teclas;
/** @brief Distancia que se muestra en el LCD; se congela cuando HOLD está activo. */
uint16_t held_distance = 0;

/*==================[internal functions declaration]=========================*/

/**
 * @brief Tarea de medición de distancia.
 *
 * Cada CONFIG_TASK_MEASURE_PERIOD ms, si la medición está habilitada
 * (@ref button_1), lee la distancia en centímetros y la guarda en
 * @ref distance. Además la imprime por consola.
 *
 *
 * @param pvParameter Parámetro de la tarea (no utilizado).
 */
static void measureTask(void *pvParameter){
    while(true){
		if (button_1){
			distance = HcSr04ReadDistanceInCentimeters();
		}

		printf("Distance: %d cm\n", distance);
		vTaskDelay(CONFIG_TASK_MEASURE_PERIOD / portTICK_PERIOD_MS);
	}
}

/**
 * @brief Tarea de visualización.
 *
 * Enciende o apaga los LEDs según el rango de @ref distance, actualiza
 * @ref held_distance mientras HOLD no esté activo y escribe ese valor
 * en el display LCD.
 *
 *
 * @param pvParameter Parámetro de la tarea (no utilizado).
 */
static void displayTask(void *pvParameter){
    while(true){

		if(distance < 10){
			LedOff(LED_1); LedOff(LED_2); LedOff(LED_3);
			printf("LEDs Off\n");
		}
		else if (distance >= 10 && distance < 20){
			LedOn(LED_1); LedOff(LED_2); LedOff(LED_3);
			printf("LED 1 On\n");
		}
		else if (distance >= 20 && distance < 30){
			LedOn(LED_1); LedOn(LED_2); LedOff(LED_3);
			printf("LED 1 On\n");
			printf("LED 2 On\n");

		}
		else if (distance >= 30){
			LedOn(LED_1); LedOn(LED_2); LedOn(LED_3);
			printf("LED 1 On\n");
			printf("LED 2 On\n");
			printf("LED 3 On\n");
		}
		if (!button_2){
			held_distance = distance;
		}
		LcdItsE0803Write(held_distance);
		vTaskDelay(CONFIG_TASK_DISPLAY_PERIOD / portTICK_PERIOD_MS);
	}
}

/**
 * @brief Tarea de lectura de teclas (polling).
 *
 * Lee las teclas cada CONFIG_TASK_BUTTON_PERIOD ms. TEC1 alterna
 * @ref button_1 (medición activa/detenida) y TEC2 alterna @ref button_2
 * (HOLD).
 *
 *
 * @param pvParameter Parámetro de la tarea (no utilizado).
 */
static void buttonTask(void *pvParameter){
    while(true){
		teclas  = SwitchesRead();
    	switch(teclas){
    		case SWITCH_1:
    			button_1 = !button_1;
    		break;
    		case SWITCH_2:
    			button_2 = !button_2;
    		break;
		}
		vTaskDelay(CONFIG_TASK_BUTTON_PERIOD / portTICK_PERIOD_MS);
		
	}
}
/*==================[external functions definition]==========================*/

/**
 * @brief Función principal de la aplicación.
 *
 * Inicializa los periféricos (teclas, LEDs, sensor HC-SR04 y display LCD)
 * y crea las tres tareas de FreeRTOS. Luego el scheduler toma el control.
 */
void app_main(void){

	SwitchesInit();
	LedsInit();
	HcSr04Init(GPIO_3, GPIO_2);
	LcdItsE0803Init();


	xTaskCreate(&measureTask, "Measure", 512, NULL, 5, &measure_task_handle);
	xTaskCreate(&displayTask, "Display", 512, NULL, 5, &display_task_handle);
	xTaskCreate(&buttonTask, "Button", 512, NULL, 5, &button_task_handle);
}
/*==================[end of file]============================================*/