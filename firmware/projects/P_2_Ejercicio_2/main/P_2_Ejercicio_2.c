/*! @mainpage Regla Ultrasonica con Timers
 *
 * @section genDesc General Description
 *
 * Regla ultrasónica con display LCD sobre la placa EDU-ESP (ESP32-C6),
 * con la misma funcionalidad que la Actividad 1 pero controlada por
 * interrupciones:
 *
 * - Dos timers por hardware (TIMER_A y TIMER_B) generan una interrupción
 *   periódica de 1 s. Su función de callback envía una notificación a la
 *   tarea correspondiente (@ref measureTask y @ref displayTask), que
 *   permanece bloqueada hasta recibirla.
 * - Las teclas se atienden por interrupción mediante SwitchActivInt(), por
 *   lo que ya no se necesita una tarea de lectura de teclas.
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
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 10/09/2026 | Document creation		                         |
 *
 * @author Amilcar Muñoz (amilcar.munioz@gmail.com)
 *
 */

/**
 * @file
 * @brief Regla ultrasónica con timers e interrupciones (Proyecto 2, Actividad 2).
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
#include "timer_mcu.h"
#include "lcditse0803.h"
/*==================[macros and definitions]=================================*/
/** @brief Período del timer de medición, en microsegundos (1.000.000 us = 1 s). */
#define CONFIG_TASK_MEASURE_PERIOD 1000000
/** @brief Período del timer de visualización, en microsegundos (1 s). */
#define CONFIG_TASK_DISPLAY_PERIOD 1000000
/** @brief Valor simbólico de LED encendido (no utilizado en esta versión). */
#define LED_ON 1
/** @brief Valor simbólico de LED apagado (no utilizado en esta versión). */
#define LED_OFF 2

/*==================[internal data definition]===============================*/
/** @brief Handle de la tarea de medición; destino de la notificación de TIMER_A. */
TaskHandle_t measure_task_handle = NULL;
/** @brief Handle de la tarea de visualización; destino de la notificación de TIMER_B. */
TaskHandle_t display_task_handle = NULL;
/** @brief Última distancia medida por el HC-SR04, en cm. */
uint16_t distance = 0;
/** @brief Indicador de medición activa (alternado por TEC1). */
bool button_1 = false;
/** @brief Indicador de modo HOLD activo (alternado por TEC2). */
bool button_2 = false;
/** @brief Distancia que se muestra en el LCD; se congela cuando HOLD está activo. */
uint16_t held_distance = 0;
/*==================[internal functions declaration]=========================*/

/**
 * @brief Función de callback invocada en la interrupción del timer A.
 *
 * Envía una notificación a @ref measureTask, que estaba bloqueada
 * esperándola, para que ejecute una nueva medición.
 *
 * @param param Parámetro de usuario del timer.
 */
void FuncTimerA(void* param){
    vTaskNotifyGiveFromISR(measure_task_handle, pdFALSE);    /* Envía una notificación a la tarea asociada a measure */
}

/**
 * @brief Función de callback invocada en la interrupción del timer B.
 *
 * Envía una notificación a @ref displayTask, que estaba bloqueada
 * esperándola, para que actualice LEDs y display.
 *
 * @param param Parámetro de usuario del timer.
 */
void FuncTimerB(void* param){
    vTaskNotifyGiveFromISR(display_task_handle, pdFALSE);    /* Envía una notificación a la tarea asociada a display */
}

/**
 * @brief Tarea de medición de distancia.
 *
 * Permanece bloqueada en ulTaskNotifyTake() hasta que @ref FuncTimerA la
 * notifica. Si la medición está habilitada (@ref button_1), lee la
 * distancia en centímetros y la guarda en @ref distance. Además la imprime
 * por consola.
 *
 * @param pvParameter Parámetro de la tarea.
 */
static void measureTask(void *pvParameter){
    while(true){
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		if (button_1){
			distance = HcSr04ReadDistanceInCentimeters();
		}

		printf("Distance: %d cm\n", distance);
		// vTaskDelay(CONFIG_TASK_MEASURE_PERIOD / portTICK_PERIOD_MS);
	}
}

/**
 * @brief Tarea de visualización.
 *
 * Permanece bloqueada en ulTaskNotifyTake() hasta que @ref FuncTimerB la
 * notifica. Luego enciende o apaga los LEDs según el rango de
 * @ref distance, actualiza @ref held_distance mientras HOLD no esté activo
 * y escribe ese valor en el display LCD.
 *
 * @param pvParameter Parámetro de la tarea.
 */
static void displayTask(void *pvParameter){
    while(true){
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);	/* La tarea espera en este punto hasta recibir una notificación */
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
		// vTaskDelay(CONFIG_TASK_DISPLAY_PERIOD / portTICK_PERIOD_MS);
	}
}

/*==================[external functions definition]==========================*/

/**
 * @brief Función de callback de la interrupción de TEC1.
 *
 * Alterna el indicador @ref button_1 (medición activa/detenida).
 */
void switch1Action(){
	button_1 = !button_1;
}

/**
 * @brief Función de callback de la interrupción de TEC2.
 *
 * Alterna el indicador @ref button_2 (modo HOLD).
 */
void switch2Action(){
	button_2 = !button_2;
}

/**
 * @brief Función principal de la aplicación.
 *
 * Inicializa los periféricos (teclas, LEDs, sensor HC-SR04 y display LCD),
 * asocia las funciones de callback a las interrupciones de TEC1 y TEC2,
 * configura e inicializa los timers A y B, crea las tareas de medición y
 * visualización, y por último inicia el conteo de ambos timers.
 */
void app_main(void){
	
	SwitchesInit();  /* Inicialización de switches */
	LedsInit(); 	/* Inicialización de LEDs */
	HcSr04Init(GPIO_3, GPIO_2); /* inicialización del sensor HC-SR04 */
	LcdItsE0803Init();
	
	/* Utilozando la funcion SwitchActivInt*/
    SwitchActivInt(SWITCH_1, switch1Action, NULL);
    SwitchActivInt(SWITCH_2, switch2Action, NULL);

	
	   /* Inicialización de timers */
    timer_config_t timer_measure = {
        .timer = TIMER_A,
        .period = CONFIG_TASK_MEASURE_PERIOD,
        .func_p = FuncTimerA,
        .param_p = NULL
    };
    TimerInit(&timer_measure);   

	timer_config_t timer_display = {
        .timer = TIMER_B,
        .period = CONFIG_TASK_DISPLAY_PERIOD,
        .func_p = FuncTimerB,
        .param_p = NULL
    };
    TimerInit(&timer_display);


	xTaskCreate(&measureTask, "Measure", 512, NULL, 5, &measure_task_handle);
	xTaskCreate(&displayTask, "Display", 512, NULL, 5, &display_task_handle);
	    /* Inicialización del conteo de timers */
    TimerStart(timer_measure.timer);
    TimerStart(timer_display.timer);
}
/*==================[end of file]============================================*/