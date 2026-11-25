/**
 * @file all_in_one.h
 * @brief Interfaz gráfica de smartwatch para display redondo GC9A01 (240×240) con LVGL 9.x.
 *
 * Provee tres vistas conmutables renderizadas sobre un display TFT circular
 * de 1.28" controlado por el GC9A01:
 *   - @ref ALL_IN_ONE_VIEW_ALL    : reloj analógico + HR + SpO2 + curva PPG reducida.
 *   - @ref ALL_IN_ONE_VIEW_CLOCK  : reloj analógico a pantalla completa.
 *   - @ref ALL_IN_ONE_VIEW_PARAMS : HR + SpO2 + curva PPG ampliada con anillo animado de latido.
 *
 * @note Todas las funciones de este módulo deben llamarse desde la tarea que
 * ejecuta lv_timer_handler() (DisplayTask en Proyecto_Integrador.c).
 *
 * @author Amilcar Muñoz (amilcar.munioz@gmail.com)
 */
#ifndef ALL_IN_ONE_H
#define ALL_IN_ONE_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Vistas disponibles en la interfaz gráfica. */
typedef enum {
    ALL_IN_ONE_VIEW_ALL = 0,    /*!< Reloj analógico + HR + SpO2 + curva PPG reducida */
    ALL_IN_ONE_VIEW_CLOCK,      /*!< Reloj analógico a pantalla completa */
    ALL_IN_ONE_VIEW_PARAMS,     /*!< HR + SpO2 + curva PPG ampliada + anillo de latido */
    ALL_IN_ONE_VIEW_COUNT       /*!< Cantidad total de vistas */
} all_in_one_view_t;

/**
 * @brief Crea la interfaz completa con las tres vistas.
 *
 * Construye los widgets de las vistas ALL, CLOCK y PARAMS dentro del contenedor
 * padre indicado (o la pantalla activa si parent es NULL). Arranca mostrando
 * ALL_IN_ONE_VIEW_ALL; las demás vistas se ocultan.
 *
 * @param[in] parent Contenedor padre de LVGL (NULL = pantalla activa).
 * @return Puntero al contenedor raíz de 240×240 creado.
 */
lv_obj_t *all_in_one_create(lv_obj_t *parent);

/**
 * @brief Cambia a la vista indicada.
 * @param[in] view Vista destino (ALL, CLOCK o PARAMS).
 */
void all_in_one_set_view(all_in_one_view_t view);

/**
 * @brief Pasa a la siguiente vista en orden cíclico (ALL → CLOCK → PARAMS → ALL).
 */
void all_in_one_next_view(void);

/**
 * @brief Devuelve la vista actualmente visible.
 * @return Vista actual.
 */
all_in_one_view_t all_in_one_get_view(void);

/**
 * @brief Fija la hora del reloj analógico.
 *
 * El segundero avanza automáticamente cada 1 s mediante un timer interno de LVGL.
 *
 * @param[in] h Hora (0-23).
 * @param[in] m Minutos (0-59).
 * @param[in] s Segundos (0-59).
 */
void all_in_one_set_time(uint8_t h, uint8_t m, uint8_t s);

/**
 * @brief Actualiza la frecuencia cardíaca en todas las vistas.
 * @param[in] bpm Frecuencia cardíaca en BPM. Valores ≤ 0 muestran "--".
 */
void all_in_one_set_hr(int bpm);

/**
 * @brief Actualiza la saturación de oxígeno en todas las vistas.
 * @param[in] pct SpO2 en porcentaje. Valores ≤ 0 muestran "--".
 */
void all_in_one_set_spo2(int pct);

/**
 * @brief Agrega una muestra a la curva PPG en todas las vistas.
 * @param[in] value_0_1000 Valor normalizado en el rango 0-1000 (se satura internamente).
 */
void all_in_one_ppg_push(int value_0_1000);

/**
 * @brief Dispara la animación de latido en la vista PARAMS.
 *
 * El anillo celeste se expande hacia el borde del display y se desvanece.
 * Debe llamarse en el instante en que se detecta cada latido.
 */
void all_in_one_beat(void);

/**
 * @brief Habilita o deshabilita el modo de latido automático.
 *
 * En modo automático, el anillo late al ritmo del último HR recibido
 * sin necesidad de detección externa de cada pico.
 *
 * @param[in] enable true para activar, false para desactivar.
 */
void all_in_one_set_beat_auto(bool enable);

/**
 * @brief Inicia una demostración con datos sintéticos.
 *
 * Genera una señal PPG artificial con HR de 72 BPM y SpO2 de 98% para
 * verificar la interfaz gráfica sin necesidad de conectar el sensor.
 */
void all_in_one_demo_start(void);

#ifdef __cplusplus
}
#endif

#endif /* ALL_IN_ONE_H */