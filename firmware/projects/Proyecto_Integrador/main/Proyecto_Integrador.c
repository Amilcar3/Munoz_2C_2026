/*! @mainpage Proyecto Integrador - Prototipo de Smartwatch
 *
 * @section genDesc Descripción General
 *
 * Firmwar del Prototipo de Smartwatch basado en la placa ESP-EDU (ESP32-C6) usando los modulos MAX30102 y GC9A *01.
 * El sistema adquiere señales fotopletismográficas (PPG)
 * mediante un sensor óptico MAX30102, calcula en tiempo real la frecuencia cardíaca
 * (HR) y la saturación de oxígeno en sangre (SpO2), y presenta los resultados en un
 * display TFT redondo GC9A01 de 1.28" (240×240 px) con interfaz gráfica LVGL.
 *
 * La interfaz de usuario ofrece tres vistas intercambiables:
 * - **ALL**: reloj analógico + HR + SpO2 + curva PPG reducida.
 * - **CLOCK**: reloj analógico a pantalla completa.
 * - **PARAMS**: HR + SpO2 + curva PPG ampliada con anillo de latido animado.
 *
 * El cambio de vista se controla mediante SWITCH_1 (GPIO_4) o por BLE desde una
 * aplicación móvil. Los datos de HR, SpO2 y la señal PPG se transmiten
 * periódicamente por BLE para su visualización remota.
 *
 * La arquitectura del firmware se organiza en cuatro tareas FreeRTOS comunicadas
 * mediante colas:
 * - **readTask** (prioridad 5): lectura de la FIFO del MAX30102 a 100 Hz.
 * - **ProcessTask** (prioridad 4): filtrado de la señal PPG, cálculo de HR/SpO2
 *   y detección de latidos.
 * - **DisplayTask** (prioridad 3): renderizado de la interfaz gráfica con LVGL.
 * - **BleTask** (prioridad 2): transmisión de datos por Bluetooth Low Energy.
 *
 * @section hardConn Conexión de Hardware
 *
 * @subsection displayConn Display GC9A01 (SPI)
 *
 * |   Display GC9A01   |   ESP-EDU          |
 * |:------------------:|:-------------------|
 * | 	SCL / CLK       |	GPIO_20          |
 * | 	SDA / DIN (MOSI)| 	GPIO_21          |
 * | 	CS              | 	GPIO_19 (SPI_1)  |
 * | 	DC              | 	GPIO_9           |
 * | 	RST             | 	GPIO_18          |
 * | 	VCC             | 	3V3              |
 * | 	GND             | 	GND              |
 *
 * @subsection sensorConn Sensor MAX30102 (I2C)
 *
 * |   MAX30102         |   ESP-EDU          |
 * |:------------------:|:-------------------|
 * | 	SDA             |	GPIO_6           |
 * | 	SCL             | 	GPIO_7           |
 * | 	VCC             | 	3V3              |
 * | 	GND             | 	GND              |
 *
 * @subsection switchConn Pulsador
 *
 * |   Pulsador         |   ESP-EDU          |
 * |:------------------:|:-------------------|
 * | 	SWITCH_1        |	GPIO_4           |
 *
 * @section changelog Changelog
 *
 * |   Date	    | Description                                    |
 * |:----------:|:-----------------------------------------------|
 * | 04/11/2026 | Creación del documento                         |
 *
 * @author Amilcar Muñoz (amilcar.munioz@gmail.com)
 *
 */

/*==================[inclusions]=============================================*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "all_in_one.h"
#include "gc9a01.h"
#include "gpio_mcu.h"
#include <math.h>
#include "freertos/queue.h"
#include "heartRate.h"
#include "max3010x.h"
#include "spo2_algorithm.h"
#include <iir_filter.h>
#include "ble_mcu.h"
#include "switch.h"
/*==================[macros and definitions]=================================*/
#define SAMPLE_FREQ        50       /*!< Frecuencia de muestreo del sensor MAX30102 en Hz */
#define HR_AVG_SIZE        4        /*!< Cantidad de muestras para el promedio móvil de HR */
#define LED_POWER          60       /*!< Potencia del LED del MAX30102 (0-255) */
#define FINGER_THRESHOLD   50000    /*!< Umbral de señal IR para detectar presencia de dedo */
#define READ_POLL_MS       10       /*!< Período de polling de la FIFO del sensor en ms */
#define SAMPLE_QUEUE_LEN   100      /*!< Longitud de la cola de muestras entre readTask y ProcessTask */
#define PPG_CENTER         500.0f   /*!< Centro vertical del chart en unidades LVGL (rango 0-1000) */
#define PPG_TARGET_HEIGHT  350.0f   /*!< Amplitud pico a pico deseada en pantalla */
#define PPG_INITIAL_SPAN   1000.0f  /*!< Amplitud pico a pico inicial antes de normalizar */
#define PPG_DECIM          1        /*!< Factor de decimación de la PPG hacia el display (1 = sin decimación) */
#define BUF_LINES   40              /*!< Filas que LVGL dibuja de una vez */
#define DISPLAY_QUEUE_LEN 32        /*!< Longitud de la cola de mensajes hacia DisplayTask */
/*==================[internal data definition]===============================*/

/** @brief Buffer de dibujo para LVGL (renderizado parcial por filas) */
static uint8_t draw_buf[GC9A01_WIDTH * BUF_LINES * 2] __attribute__((aligned(64)));

/**
 * @brief Tipos de mensajes intercambiados entre ProcessTask y DisplayTask.
 */
typedef enum {
    MSG_PPG_SAMPLE, /*!< Muestra PPG normalizada para el chart */
    MSG_VITALS,     /*!< HR y SpO2 calculados */
    MSG_KEY,        /*!< Comando de cambio de vista (tecla o BLE) */
    MSG_BEAT        /*!< Pico de latido detectado por checkForBeat() */
} msg_type_t;

/**
 * @brief Mensaje enviado a la cola de display.
 *
 * Contiene el tipo de mensaje y un union con los datos correspondientes.
 */
typedef struct {
    msg_type_t type;    /*!< Tipo de mensaje */
    union {
        int16_t ppg;                                  /*!< Valor PPG normalizado (MSG_PPG_SAMPLE) */
        struct { uint8_t hr; uint8_t spo2; } vitals;  /*!< HR en BPM y SpO2 en % (MSG_VITALS) */
        uint8_t key;                                  /*!< Código de tecla (MSG_KEY) */
    };
} display_msg_t;

static QueueHandle_t xQueueDisplay = NULL;  /*!< Cola de mensajes hacia DisplayTask */

/**
 * @brief Muestra cruda del sensor MAX30102.
 */
typedef struct {
    uint32_t red;   /*!< Valor del canal rojo */
    uint32_t ir;    /*!< Valor del canal infrarrojo */
} sample_t;

static QueueHandle_t xQueueSamples = NULL;      /*!< Cola de muestras entre readTask y ProcessTask */
static float dato, dato_hp, dato_lp;            /*!< Variables de los filtros IIR */
static uint32_t irBuffer[200];                   /*!< Buffer circular de muestras IR para el algoritmo SpO2 */
static uint32_t redBuffer[200];                  /*!< Buffer circular de muestras RED para el algoritmo SpO2 */
static int32_t bufferLength = 200;               /*!< Longitud del buffer de muestras */
static int32_t spo2, heartRate, hr_display = 0;  /*!< Valores calculados de SpO2, HR y HR promediado */
static int8_t validSPO2, validHeartRate;         /*!< Flags de validez de los cálculos */
static int32_t hr_buffer[HR_AVG_SIZE];           /*!< Buffer para promedio móvil de HR */
static uint8_t hr_idx = 0;                       /*!< Índice del promedio móvil de HR */
static uint32_t samples_lost = 0;                /*!< Contador de muestras descartadas por cola llena */

/** @name Variables compartidas con BleTask
 *  @brief Valores actualizados por ProcessTask y leídos por BleTask para la transmisión BLE.
 *  @{
 */
static volatile uint8_t  g_hr_ble  = 0;    /*!< Frecuencia cardíaca para envío BLE */
static volatile uint8_t  g_spo2_ble = 0;   /*!< SpO2 para envío BLE */
static volatile int16_t  g_ppg_ble = 0;    /*!< Señal PPG normalizada para envío BLE */
/** @} */
/*==================[internal functions declaration]=========================*/

/**
 * @brief ISR de SWITCH_1: envía MSG_KEY='N' (next view) a la cola de display.
 *        Se ejecuta en contexto de interrupción de GPIO, por eso usa FromISR.
 */
static void IRAM_ATTR Switch1_ISR(void *args){
    BaseType_t woken = pdFALSE;
    display_msg_t msg = { .type = MSG_KEY, .key = 'N' };
    xQueueSendFromISR(xQueueDisplay, &msg, &woken);
    portYIELD_FROM_ISR(woken);
}

/**
 * @brief Callback BLE: recibe comandos de la app para cambiar de vista.
 *        Protocolo de un solo carácter:
 *          'N'       → next view
 *          '0','1','2' → ir directo a ALL / CLOCK / PARAMS
 */
static void BleReadCb(uint8_t *data, uint8_t length){
    if(length == 0) return;
    switch(data[0]){
        case 'N':
        case '0':
        case '1':
        case '2': {
            display_msg_t msg = { .type = MSG_KEY, .key = data[0] };
            xQueueSend(xQueueDisplay, &msg, 0);    /* callback BLE no es ISR real */
            break;
        }
        default:
            break;
    }
}


/**
 * @brief Callback de tick para LVGL.
 * @return Tiempo transcurrido en milisegundos desde el inicio del sistema.
 */
static uint32_t TickCb(void){
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/**
 * @brief Callback de flush para LVGL.
 *
 * Convierte el formato de color y envía la región renderizada al display GC9A01.
 *
 * @param[in] disp   Display de LVGL que solicita el flush.
 * @param[in] area   Área rectangular a actualizar.
 * @param[in] px_map Puntero al buffer de píxeles renderizados (RGB565).
 */
static void FlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map){
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    lv_draw_sw_rgb565_swap(px_map, w * h);
    GC9A01DrawPicture(area->x1, area->y1, w, h, px_map);
    lv_display_flush_ready(disp);
}

/**
 * @brief Inicializa el display GC9A01 y el subsistema LVGL.
 *
 * Configura el puerto SPI_1 con los pines DC (GPIO_9) y RST (GPIO_18),
 * crea el display de LVGL con buffer de renderizado parcial y registra
 * los callbacks de tick y flush.
 *
 * @note Se invoca una sola vez al inicio de DisplayTask.
 */
static void DisplayInit(void){
    lv_display_t *display;

    GC9A01Init(SPI_1, GPIO_9, GPIO_18);
    lv_init();
    lv_tick_set_cb(TickCb);
    display = lv_display_create(GC9A01_WIDTH, GC9A01_HEIGHT);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buf, NULL, sizeof(draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, FlushCb);
}


/**
 * @brief Tarea de lectura del sensor MAX30102.
 *
 * Inicializa el sensor con la potencia y modo configurados, luego en un bucle
 * periódico de @ref READ_POLL_MS ms lee todas las muestras disponibles en la
 * FIFO del MAX30102 y las envía a @ref xQueueSamples para su procesamiento.
 *
 * @param[in] pvParameter No utilizado.
 */
static void readTask(void *pvParameter){
    sample_t s;
    TickType_t last = xTaskGetTickCount();

    MAX3010X_begin();
    MAX3010X_setup(LED_POWER, 2, 2, 100, 411, 4096);

    while(true){
        vTaskDelayUntil(&last, pdMS_TO_TICKS(READ_POLL_MS));

        MAX3010X_check();                       /* trae lo nuevo de la FIFO */
        while(MAX3010X_available()){            /* y se vacía por completo */
            s.red = MAX3010X_getFIFORed();
            s.ir  = MAX3010X_getFIFOIR();
            MAX3010X_nextSample();
            if(xQueueSend(xQueueSamples, &s, 0) != pdTRUE){
                samples_lost++;                 /* cola llena: contamos, no bloqueamos */
            }
        }
    }
}
/**
 * @brief Tarea de procesamiento de señales.
 *
 * Recibe muestras del sensor desde @ref xQueueSamples y realiza:
 * - Filtrado pasa-altos y pasa-bajos IIR de la señal PPG.
 * - Normalización dinámica de la amplitud para la visualización.
 * - Detección de latidos mediante checkForBeat().
 * - Cálculo de HR y SpO2 cada @ref SAMPLE_FREQ muestras mediante el algoritmo
 *   de Maxim (maxim_heart_rate_and_oxygen_saturation()).
 * - Promedio móvil de HR con ventana de @ref HR_AVG_SIZE muestras.
 * - Envío de mensajes MSG_PPG_SAMPLE, MSG_VITALS y MSG_BEAT a @ref xQueueDisplay.
 */
static void ProcessTask(void *pvParameter){
    sample_t s;
    display_msg_t msg;
    uint16_t new_count = 0;
    uint16_t i;
    static float ppg_span = PPG_INITIAL_SPAN;
    static float win_min = 1e6f, win_max = -1e6f;
    static uint8_t ppg_decim = 0;   /* decimador: 1 de cada PPG_DECIM muestras al display */

    HiPassInit(SAMPLE_FREQ, 0.5, ORDER_4);
    LowPassInit(SAMPLE_FREQ, 4.0, ORDER_4);

    while(true){
        xQueueReceive(xQueueSamples, &s, portMAX_DELAY);

        redBuffer[bufferLength - SAMPLE_FREQ + new_count] = s.red;
        irBuffer[bufferLength - SAMPLE_FREQ + new_count]  = s.ir;
        new_count++;

        /* Detector de dedo y filtrado de la PPG */
        bool finger = (s.ir >= FINGER_THRESHOLD);
        if(finger){
            dato = (float)s.red;
            HiPassFilter(&dato, &dato_hp, 1);
            LowPassFilter(&dato_hp, &dato_lp, 1);
            dato_lp = -dato_lp;                 /* Inversión: mayor absorción sístólica -> pico positivo */
            if(dato_lp < win_min) win_min = dato_lp;
            if(dato_lp > win_max) win_max = dato_lp;
        } else {
            dato_lp = 0.0f;
        }

        if(finger && checkForBeat((int32_t)s.red)){
            display_msg_t beat_msg = { .type = MSG_BEAT };
            xQueueSend(xQueueDisplay, &beat_msg, 0); /*Se detectan y envian latidos para la funcion anillo*/
        }

        float v;
        if(finger && ppg_span > 10.0f){
            float scale = PPG_TARGET_HEIGHT / ppg_span;
            v = PPG_CENTER + dato_lp * scale;
        } else {
            v = PPG_CENTER;
        }
        if(v < 50.0f) v = 50.0f;
        if(v > 950.0f) v = 950.0f;
        g_ppg_ble = (int16_t)v;                 /* actualizacion continua para BLE (~50 Hz) */
        /* Decimacion: solo 1 de cada PPG_DECIM muestras va al chart (~16 Hz) */
        if(++ppg_decim >= PPG_DECIM){
            ppg_decim = 0;
            msg.type = MSG_PPG_SAMPLE;
            msg.ppg  = (int16_t)v;
            xQueueSend(xQueueDisplay, &msg, 0);
        }

        /* Cada 50 muestras nuevas se recalcula HR y SpO2 */
        if(new_count >= SAMPLE_FREQ){
            maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer,
                                                   &spo2, &validSPO2, &heartRate, &validHeartRate);

            if(validHeartRate && (heartRate < 40 || heartRate > 200)){ validHeartRate = 0; }
            if(validSPO2 && (spo2 < 70 || spo2 > 100)){ validSPO2 = 0; }

            if(validHeartRate){
                hr_buffer[hr_idx % HR_AVG_SIZE] = heartRate;
                hr_idx++;
                int32_t sum = 0;
                uint8_t count = (hr_idx < HR_AVG_SIZE) ? hr_idx : HR_AVG_SIZE;
                for(uint8_t j = 0; j < count; j++) sum += hr_buffer[j];
                hr_display = sum / count;
            }
            if(!finger){ hr_display = 0; }          /* sin dedo, no mostrar un valor viejo */

            msg.type = MSG_VITALS;
            msg.vitals.hr   = (uint8_t)hr_display;
            msg.vitals.spo2 = (validSPO2 && finger) ? (uint8_t)spo2 : 0;
            xQueueSend(xQueueDisplay, &msg, pdMS_TO_TICKS(10));

            /* Actualiza variables compartidas para que BleTask las envíe */
            g_hr_ble   = msg.vitals.hr;
            g_spo2_ble = msg.vitals.spo2;

            /* Se corre la ventana: las últimas 150 pasan al inicio */
            for(i = SAMPLE_FREQ; i < bufferLength; i++){
                redBuffer[i - SAMPLE_FREQ] = redBuffer[i];
                irBuffer[i - SAMPLE_FREQ]  = irBuffer[i];
            }
            /* Actualización de la escala dinámica entre ventanas */
            if(finger && (win_max > win_min)){
                float current_span = win_max - win_min;
                if(current_span > 20.0f){
                    /* Suavizado entre ventana y ventana */
                    ppg_span = 0.7f * ppg_span + 0.3f * current_span;
                }
            } else if(!finger){
                ppg_span = PPG_INITIAL_SPAN;
            }
            win_min = 1e6f;
            win_max = -1e6f;

            new_count = 0;
        }
    }
}

/**
 * @brief Tarea de renderizado de la interfaz gráfica.
 *
 * Inicializa el display y la interfaz all_in_one, luego consume mensajes
 * de @ref xQueueDisplay para actualizar la curva PPG, los valores de HR/SpO2,
 * cambiar de vista o disparar la animación de latido. Ejecuta lv_timer_handler()
 * en cada iteración para que LVGL redibuje los widgets modificados.
 */
static void DisplayTask(void *pvParameter){
    uint32_t wait_ms;
    display_msg_t msg;

    DisplayInit();
    all_in_one_create(NULL);
    all_in_one_set_beat_auto(false);  /* el anillo lo dispara checkForBeat() */

    while(true){
        while(xQueueReceive(xQueueDisplay, &msg, 0) == pdTRUE){   /* no bloqueante */
            switch(msg.type){
                case MSG_PPG_SAMPLE:
                    all_in_one_ppg_push(msg.ppg);
                    break;
                case MSG_VITALS:
                    all_in_one_set_hr(msg.vitals.hr);
                    all_in_one_set_spo2(msg.vitals.spo2);
                    break;
                case MSG_KEY:
                    /* Cambio de vista desde tecla fisica (SWITCH_1) o BLE */
                    if(msg.key == 'N'){
                        all_in_one_next_view();
                    } else if(msg.key >= '0' && msg.key <= '2'){
                        all_in_one_set_view((all_in_one_view_t)(msg.key - '0'));
                    }
                    break;
                case MSG_BEAT:
                    all_in_one_beat();
                    break;
            }
        }
        wait_ms = lv_timer_handler();
        if (wait_ms < 10) wait_ms = 10;
        if (wait_ms > 20) wait_ms = 20;
        vTaskDelay(pdMS_TO_TICKS(wait_ms));
    }
}

/**
 * @brief Tarea de comunicación Bluetooth Low Energy.
 *
 * Inicializa el stack BLE con el nombre "ESP_EDU_1" y el callback de recepción
 * BleReadCb(). Mientras la conexión esté activa, envía cada 50 ms una cadena
 * con el formato:
 * @code
 *   "*H<hr>**S<spo2>**G<ppg>*"
 * @endcode
 * Ejemplo: `"*H75**S97**G512*"`
 *
 * Los comandos recibidos desde la aplicación móvil (un solo carácter) se procesan
 * en BleReadCb():
 * - `'N'` → siguiente vista.
 * - `'0'` → vista ALL.
 * - `'1'` → vista CLOCK.
 * - `'2'` → vista PARAMS.
 */
static void BleTask(void *pvParameter){
    char msg[32];
    static const ble_config_t ble_cfg = {
        .device_name = "ESP_EDU_1",
        .func_p      = BleReadCb,
    };
    BleInit((ble_config_t *)&ble_cfg);

    while(true){
        if(BleStatus() == BLE_CONNECTED){
            snprintf(msg, sizeof(msg), "*H%u**S%u**G%d*", g_hr_ble, g_spo2_ble, g_ppg_ble);
            BleSendString(msg);
        }
        vTaskDelay(pdMS_TO_TICKS(50));     /* envio cada 50 ms (~20 Hz) para graficar onda en vivo */
    }
}

/*==================[external functions definition]==========================*/

/**
 * @brief Función principal de la aplicación.
 *
 * Inicializa los periféricos (pulsadores), configura la interrupción de
 * SWITCH_1 para cambiar de vista, crea las colas de comunicación entre
 * tareas y lanza las cuatro tareas del sistema.
 */
void app_main(void){
    /* Inicializacion de perifericos */
    SwitchesInit();
    SwitchActivInt(SWITCH_1, Switch1_ISR, NULL);

    /* Creacion de colas */
    xQueueDisplay = xQueueCreate(DISPLAY_QUEUE_LEN, sizeof(display_msg_t));
    xQueueSamples = xQueueCreate(SAMPLE_QUEUE_LEN, sizeof(sample_t));

    /* Creacion de tareas */
    xTaskCreate(&readTask,    "Read",    4096,  NULL, 5, NULL);
    xTaskCreate(&ProcessTask, "Process", 8192,  NULL, 4, NULL);
    xTaskCreate(&DisplayTask, "Display", 16384, NULL, 3, NULL);
    xTaskCreate(&BleTask,     "BLE",     4096,  NULL, 2, NULL);
}

