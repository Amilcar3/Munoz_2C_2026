/*! @mainpage Osiloscopio
 *
 * @section genDesc General Description
 *
 * Osciloscopio por puerto serie sobre la placa EDU-ESP (ESP32-C6), con
 * reproducción simultánea de una señal de ECG por el conversor D/A:
 *
 * - @ref ecgTask recorre el arreglo @ref ecg y escribe cada muestra en el
 *   conversor D/A (modulador sigma-delta) mediante AnalogOutputWrite(),
 *   generando la señal analógica. Se dispara por TIMER_B.
 * - @ref adcTask digitaliza la entrada CH1 del conversor A/D a 500 Hz y
 *   transmite cada muestra por UART en formato ASCII, compatible con el
 *   Serial Plotter de VSCode. Se dispara por TIMER_A.
 * - La entrada CH1 se conecta físicamente a la salida del D/A (con el
 *   potenciómetro como atenuador/filtro, según sugiere la guía), cerrando
 *   el lazo: la señal generada por una tarea es digitalizada por la otra.
 *
 * <a href="https://drive.google.com/...">Operation Example</a>
 *
 * @section hardConn Hardware Connection
 *
 * |    Peripheral  |   ESP32   	|
 * |:--------------:|:--------------|
 * | 	ADC CH1 (entrada)	| 	GPIO1 (ADC1_CH1)		|
 * | 	Salida D/A (sigma-delta)	| 	GPIO0		|
 * | 	Potenciómetro	| 	entre GPIO0 (salida D/A) y GPIO1 (entrada CH1)	|
 * | 	UART_PC	| 	Puerto USB-UART de la placa (UART_NUM_0), 115200 baudios	|
 *
 *
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 24/09/2026 | Document creation		                         |
 *
 * @author Amilcar Muñoz (amilcar.munioz@gmail.com)
 *
 */

/**
 * @file
 * @brief Osciloscopio por UART con generación y lectura de ECG (Proyecto 2, Actividad 4).
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include "analog_io_mcu.h"
#include "timer_mcu.h"
#include "uart_mcu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/*==================[macros and definitions]=================================*/
/** @brief Período del timer de muestreo del ADC, en microsegundos (2000 us = 2 ms -> 500 Hz). */
#define CONFIG_TASK_ADC_PERIOD 2000
/** @brief Período del timer de reproducción del ECG, en microsegundos. */
#define CONFIG_TASK_ECG_PERIOD 4000
/** @brief Cantidad de muestras del arreglo @ref ecg, calculada en tiempo de compilación. */
#define ECG_LENGTH (sizeof(ecg) / sizeof(ecg[0]))
/*==================[internal data definition]===============================*/
/** @brief Handle de la tarea de muestreo del ADC; destino de la notificación de TIMER_A. */
TaskHandle_t adc_task_handle = NULL;
/** @brief Handle de la tarea de reproducción del ECG; destino de la notificación de TIMER_B. */
TaskHandle_t ecg_task_handle = NULL;
/*==================[internal functions declaration]=========================*/

/**
 * @brief Función de callback invocada en la interrupción del timer A.
 *
 * Notifica a @ref adcTask para que tome una nueva muestra del ADC.
 *
 * @param param Parámetro de usuario del timer (no utilizado).
 */
void FuncTimerA(void* param){
    vTaskNotifyGiveFromISR(adc_task_handle, pdFALSE);
}

/**
 * @brief Función de callback invocada en la interrupción del timer B.
 *
 * Notifica a @ref ecgTask para que escriba la siguiente muestra del ECG
 * en el conversor D/A.
 *
 * @param param Parámetro de usuario del timer (no utilizado).
 */
void FuncTimerECG(void* param){
    vTaskNotifyGiveFromISR(ecg_task_handle, pdFALSE);
}


TaskHandle_t main_task_handle = NULL;
/**
 * @brief Muestras de una señal de ECG, provistas por la cátedra.
 *
 * Se reproduce en bucle por el conversor D/A en @ref ecgTask.
 */
const uint16_t ecg[] = {
    76, 77, 78, 77, 79, 86, 81, 76, 84, 93, 85, 80,
    89, 95, 89, 85, 93, 98, 94, 88, 98, 105, 96, 91,
    99, 105, 101, 96, 102, 106, 101, 96, 100, 107, 101,
    94, 100, 104, 100, 91, 99, 103, 98, 91, 96, 105, 95,
    88, 95, 100, 94, 85, 93, 99, 92, 84, 91, 96, 87, 80,
    83, 92, 86, 78, 84, 89, 79, 73, 81, 83, 78, 70, 80, 82,
    79, 69, 80, 82, 81, 70, 75, 81, 77, 74, 79, 83, 82, 72,
    80, 87, 79, 76, 85, 95, 87, 81, 88, 93, 88, 84, 87, 94,
    86, 82, 85, 94, 85, 82, 85, 95, 86, 83, 92, 99, 91, 88,
    94, 98, 95, 90, 97, 105, 104, 94, 98, 114, 117, 124, 144,
    180, 210, 236, 253, 227, 171, 99, 49, 34, 29, 43, 69, 89,
    89, 90, 98, 107, 104, 98, 104, 110, 102, 98, 103, 111, 101,
    94, 103, 108, 102, 95, 97, 106, 100, 92, 101, 103, 100, 94, 98,
    103, 96, 90, 98, 103, 97, 90, 99, 104, 95, 90, 99, 104, 100, 93,
    100, 106, 101, 93, 101, 105, 103, 96, 105, 112, 105, 99, 103, 108,
    99, 96, 102, 106, 99, 90, 92, 100, 87, 80, 82, 88, 77, 69, 75, 79,
    74, 67, 71, 78, 72, 67, 73, 81, 77, 71, 75, 84, 79, 77, 77, 76, 76,
};
// {
// 17,17,17,17,17,17,17,17,17,17,17,18,18,18,17,17,17,17,17,17,17,18,18,18,18,18,18,18,17,17,16,16,16,16,17,17,18,18,18,17,17,17,17,
// 18,18,19,21,22,24,25,26,27,28,29,31,32,33,34,34,35,37,38,37,34,29,24,19,15,14,15,16,17,17,17,16,15,14,13,13,13,13,13,13,13,12,12,
// 10,6,2,3,15,43,88,145,199,237,252,242,211,167,117,70,35,16,14,22,32,38,37,32,27,24,24,26,27,28,28,27,28,28,30,31,31,31,32,33,34,36,
// 38,39,40,41,42,43,45,47,49,51,53,55,57,60,62,65,68,71,75,79,83,87,92,97,101,106,111,116,121,125,129,133,136,138,139,140,140,139,137,
// 133,129,123,117,109,101,92,84,77,70,64,58,52,47,42,39,36,34,31,30,28,27,26,25,25,25,25,25,25,25,25,24,24,24,24,25,25,25,25,25,25,25,
// 24,24,24,24,24,24,24,24,23,23,22,22,21,21,21,20,20,20,20,20,19,19,18,18,18,19,19,19,19,18,17,17,18,18,18,18,18,18,18,18,17,17,17,17,
// 17,17,17
// } ;

/**
 * @brief Tarea de muestreo del ADC y transmisión por UART.
 *
 * Permanece bloqueada en ulTaskNotifyTake() hasta que @ref FuncTimerA la
 * notifica (cada 2 ms, 500 Hz). Lee el canal CH1 en modo ADC_SINGLE y
 * transmite el valor por UART con el formato ">ch1:valor\r\n", compatible
 * con el Serial Plotter de VSCode.
 *
 * @note AnalogInputReadSingle() invoca internamente a adc_oneshot_read()
 * de ESP-IDF, que escribe un int (4 bytes) en el puntero recibido. Por
 * eso se le pasa la dirección de una variable int local (@c raw) en vez
 * de un uint16_t directo, para no escribir fuera de los límites de la
 * variable.
 *
 * @param pvParameter Parámetro de la tarea (no utilizado).
 */
static void adcTask(void *pvParameter){
    while(true){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int raw = 0;
        AnalogInputReadSingle(CH1, (uint16_t*)&raw);
        uint16_t adc_value = (uint16_t)raw;

        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d\r\n", adc_value);
        UartSendString(UART_PC, buffer);
    }
}

/**
 * @brief Tarea de reproducción de la señal de ECG por el conversor D/A.
 *
 * Permanece bloqueada en ulTaskNotifyTake() hasta que @ref FuncTimerECG
 * la notifica. En cada notificación escribe la muestra actual de
 * @ref ecg en el D/A y avanza el índice, volviendo al principio del
 * arreglo al llegar al final (reproducción en bucle).
 *
 * @param pvParameter Parámetro de la tarea (no utilizado).
 */
static void ecgTask(void *pvParameter){
    uint16_t i = 0;
    while(true){
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        AnalogOutputWrite(ecg[i]);
        i = (i + 1) % ECG_LENGTH;   // vuelve al principio al llegar al final (reproducción en loop)
    }
}
/*==================[external functions definition]==========================*/

/**
 * @brief Función principal de la aplicación.
 *
 * Inicializa el canal CH1 del ADC en modo ADC_SINGLE, configura e
 * inicializa el timer A (muestreo, 500 Hz), inicializa la UART sin
 * callback de recepción (UART_NO_INT, esta actividad solo transmite),
 * inicializa el conversor D/A, configura e inicializa el timer B
 * (reproducción del ECG), crea ambas tareas y por último inicia el
 * conteo de los dos timers.
 */
void app_main(void){
    analog_input_config_t adc_config = {
        .input = CH1,
        .mode = ADC_SINGLE,
    };
    AnalogInputInit(&adc_config);

    timer_config_t timer_adc = {
        .timer = TIMER_A,
        .period = CONFIG_TASK_ADC_PERIOD,
        .func_p = FuncTimerA,
        .param_p = NULL
    };
    TimerInit(&timer_adc);

    serial_config_t my_uart = {
        .port      = UART_PC,
        .baud_rate = 115200,
        .func_p    = UART_NO_INT,   // no recibimos nada por ahora
        .param_p   = NULL
    };
    UartInit(&my_uart);

	AnalogOutputInit();   // inicializa el DAC

	timer_config_t timer_ecg = {
		.timer = TIMER_B,   // un timer distinto del que usa el ADC (TIMER_A)
		.period = CONFIG_TASK_ECG_PERIOD,
		.func_p = FuncTimerECG,
		.param_p = NULL
	};
	TimerInit(&timer_ecg);

	xTaskCreate(&adcTask, "ADC", 2048, NULL, 5, &adc_task_handle);
	xTaskCreate(&ecgTask, "ECG_OUT", 2048, NULL, 5, &ecg_task_handle);

	TimerStart(timer_adc.timer);
	TimerStart(timer_ecg.timer);
	}
/*==================[end of file]============================================*/