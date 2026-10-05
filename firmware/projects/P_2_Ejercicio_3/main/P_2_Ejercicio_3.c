/*! @mainpage Regla Ultrasonica con Timers y Puerto serie
 *
 * @section genDesc General Description
 *
 * Regla ultrasónica con display LCD sobre la placa EDU-ESP (ESP32-C6),
 * controlada por interrupciones y con comunicación por puerto serie:
 *
 * - Dos timers por hardware (TIMER_A y TIMER_B) generan interrupciones
 *   periódicas. Su función de callback notifica a la tarea correspondiente
 *   (@ref measureTask y @ref displayTask).
 * - Las teclas TEC1 y TEC2 se atienden por interrupción (SwitchActivInt()).
 * - La UART (UART_PC) transmite la medición a la PC con el formato
 *   "valor unidad\r\n" y recibe comandos de un carácter, atendidos en
 *   @ref FuncUart.
 *
 * Funcionalidad:
 * - TEC1 o tecla 'O': activa/detiene la medición.
 * - TEC2 o tecla 'H': activa/desactiva el modo HOLD.
 * - LEDs según la distancia: < 10 cm todos apagados, 10-20 cm LED_1,
 *   20-30 cm LED_1 y LED_2, > 30 cm LED_1, LED_2 y LED_3.
 *
 * Comandos opcionales por UART:
 * - 'I': alterna la unidad de trabajo entre centímetros y pulgadas.
 * - 'M': alterna entre mostrar la distancia actual y la máxima medida.
 * - 'F': aumenta la velocidad de lectura (reduce 100 ms el período de medición).
 * - 'S': disminuye la velocidad de lectura (aumenta 100 ms el período de medición).
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
 * | 	UART_PC	| 	Puerto USB-UART de la placa (UART_NUM_0), 115200 baudios	|
 *
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 17/09/2026 | Document creation		                         |
 *
 * @author Amilcar Muñoz (amilcar.munioz@gmail.com)
 *
 */

/**
 * @file
 * @brief Regla ultrasónica con timers, interrupciones y puerto serie (Proyecto 2, Actividad 3).
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
#include "uart_mcu.h"
/*==================[macros and definitions]=================================*/
/** @brief Período inicial del timer de medición, en microsegundos (1.000.000 us = 1 s). */
#define CONFIG_TASK_MEASURE_PERIOD 1000000
/** @brief Período del timer de visualización, en microsegundos (1.000.000 us = 1 s). */
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
/** @brief Última distancia medida, en la unidad activa (cm o pulgadas). */
uint16_t distance = 0;
/** @brief Indicador de medición activa (alternado por TEC1 o tecla 'O'). */
bool button_1 = false;
/** @brief Indicador de modo HOLD activo (alternado por TEC2 o tecla 'H'). */
bool button_2 = false;
/** @brief Distancia que se muestra; se congela cuando HOLD está activo. */
uint16_t held_distance = 0;
/** @brief Período actual del timer de medición, en microsegundos. Se modifica con las teclas 'F' y 'S'. */
uint32_t measure_period = CONFIG_TASK_MEASURE_PERIOD;
/** @brief Indicador de unidad de trabajo: true = pulgadas, false = centímetros. */
bool unit_inches = false;
/** @brief Máxima distancia medida desde el inicio. */
uint16_t max_distance = 0;
/** @brief Indicador de visualización: true = se muestra la máxima distancia, false = la actual. */
bool show_max = false;
/*==================[internal functions declaration]=========================*/

/**
 * @brief Función de callback invocada en la interrupción del timer A.
 *
 * Envía una notificación a @ref measureTask para que realice una medición.
 *
 * @param param Parámetro de usuario del timer (no utilizado).
 */
void FuncTimerA(void* param){
    vTaskNotifyGiveFromISR(measure_task_handle, pdFALSE);    /* Envía una notificación a la tarea asociada a measure */
}

/**
 * @brief Función de callback invocada en la interrupción del timer B.
 *
 * Envía una notificación a @ref displayTask para que actualice LEDs,
 * display LCD y transmisión por UART.
 *
 * @param param Parámetro de usuario del timer (no utilizado).
 */
void FuncTimerB(void* param){
    vTaskNotifyGiveFromISR(display_task_handle, pdFALSE);    /* Envía una notificación a la tarea asociada a display */
}

/**
 * @brief Tarea de medición de distancia.
 *
 * Permanece bloqueada en ulTaskNotifyTake() hasta que @ref FuncTimerA la
 * notifica. Si la medición está habilitada (@ref button_1), lee la
 * distancia en la unidad activa (@ref unit_inches) y actualiza
 * @ref max_distance si corresponde.
 *
 * @param pvParameter Parámetro de la tarea (no utilizado).
 */
static void measureTask(void *pvParameter){
    while(true){
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		if (button_1){
			distance = unit_inches ? HcSr04ReadDistanceInInches() : HcSr04ReadDistanceInCentimeters();
    			if (distance > max_distance) max_distance = distance;
		}

		// vTaskDelay(CONFIG_TASK_MEASURE_PERIOD / portTICK_PERIOD_MS);
	}
}

/**
 * @brief Tarea de visualización y transmisión.
 *
 * Permanece bloqueada en ulTaskNotifyTake() hasta que @ref FuncTimerB la
 * notifica. Luego:
 * - Enciende o apaga los LEDs según el rango de @ref distance.
 * - Actualiza @ref held_distance mientras HOLD no esté activo.
 * - Elige el valor a mostrar: la máxima (@ref max_distance) si
 *   @ref show_max es true, o @ref held_distance en caso contrario.
 * - Escribe ese valor en el display LCD y lo transmite por UART con el
 *   formato "valor unidad\r\n".
 *
 * @param pvParameter Parámetro de la tarea (no utilizado).
 */
static void displayTask(void *pvParameter){
    while(true){
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);	/* La tarea espera en este punto hasta recibir una notificación */
		if(distance < 10){
			LedOff(LED_1); LedOff(LED_2); LedOff(LED_3);
			// printf("LEDs Off\n");
		}
		else if (distance >= 10 && distance < 20){
			LedOn(LED_1); LedOff(LED_2); LedOff(LED_3);
			// printf("LED 1 On\n");
		}
		else if (distance >= 20 && distance < 30){
			LedOn(LED_1); LedOn(LED_2); LedOff(LED_3);
			// printf("LED 1 On\n");
			// printf("LED 2 On\n");

		}
		else if (distance >= 30){
			LedOn(LED_1); LedOn(LED_2); LedOn(LED_3);
			// printf("LED 1 On\n");
			// printf("LED 2 On\n");
			// printf("LED 3 On\n");
		}
		if (!button_2){
			held_distance = distance;
		}
		uint16_t value_to_show = show_max ? max_distance : held_distance;
		LcdItsE0803Write(value_to_show);
		UartSendString(UART_PC, (char *) UartItoa(value_to_show, 10));  // base 10 -> "1024"
		UartSendString(UART_PC, " ");
		UartSendString(UART_PC, unit_inches ? "in" : "cm" );
		UartSendString(UART_PC, "\r\n");
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
 * @brief Función de callback de recepción de la UART.
 *
 * El driver la invoca cada vez que llega un byte desde la PC (evento
 * UART_DATA). Lee el carácter recibido y ejecuta el comando asociado:
 *
 * | Tecla   | Acción                                                       |
 * |:-------:|:-------------------------------------------------------------|
 * | 'O'/'o' | Alterna la medición (equivale a TEC1).                       |
 * | 'H'/'h' | Alterna el modo HOLD (equivale a TEC2).                      |
 * | 'I'/'i' | Alterna la unidad entre centímetros y pulgadas.              |
 * | 'M'/'m' | Alterna entre mostrar la distancia actual y la máxima.       |
 * | 'F'/'f' | Reduce 100 ms el período de medición (lectura más rápida).   |
 * | 'S'/'s' | Aumenta 100 ms el período de medición (lectura más lenta).   |
 *
 * El período de medición se limita a un mínimo de 100 ms para evitar que
 * la variable sin signo @ref measure_period llegue a cero o desborde.
 *
 * @param param Parámetro de usuario de la UART (no utilizado).
 */
void FuncUart(void* param){

	uint8_t c;
    UartReadByte(UART_PC, &c);
    switch(c){
        case 'O':
        case 'o':
            button_1 = !button_1;
            break;
        case 'H':
        case 'h':
            button_2 = !button_2;
            break;
		case 'I':
		case 'i':
			switch (unit_inches) {
				case true:
    				unit_inches = !unit_inches;
					distance = (uint16_t)(distance * 2.54);  // convertir a cm
					max_distance = (uint16_t)(max_distance * 2.54);  // convertir a cm
					held_distance = (uint16_t)(held_distance * 2.54);
					break;
				case false:
					unit_inches = !unit_inches;	
					distance = (uint16_t)(distance / 2.54);  // convertir a pulgadas
					max_distance = (uint16_t)(max_distance / 2.54);  // convertir a pulgadas
					held_distance = (uint16_t)(held_distance / 2.54);
					break;

			}
			break;
		case 'M':
		case 'm':	
			show_max = !show_max;
			break;
		case 'F':
		case 'f':
			if (measure_period > 100000){         // evitá que llegue a 0 o se vuelva negativo
				measure_period -= 100000;
				TimerUpdatePeriod(TIMER_A, measure_period);
			}
			break;
		case 'S':
		case 's':
			measure_period += 100000;
			TimerUpdatePeriod(TIMER_A, measure_period);
			break;
    }

}

/**
 * @brief Función principal de la aplicación.
 *
 * Inicializa los periféricos (teclas, LEDs, sensor HC-SR04, display LCD),
 * asocia las interrupciones de TEC1 y TEC2, configura e inicializa los
 * timers A y B y la UART (115200 baudios, con @ref FuncUart como callback
 * de recepción), crea las tareas y por último inicia el conteo de los timers.
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
	/*Estructura de UART*/
	serial_config_t my_uart = {
		.port      = UART_PC,   	// UART_PC o UART_CONNECTOR
		.baud_rate = 115200,    	// tiene que coincidir con el otro extremo
		.func_p    = FuncUart,  	// callback de recepción: se invoca al llegar un byte
		.param_p   = NULL
	};
	UartInit(&my_uart);

	xTaskCreate(&measureTask, "Measure", 512, NULL, 5, &measure_task_handle);
	xTaskCreate(&displayTask, "Display", 512, NULL, 5, &display_task_handle);
	    /* Inicialización del conteo de timers */
    TimerStart(timer_measure.timer);
    TimerStart(timer_display.timer);
}
/*==================[end of file]============================================*/