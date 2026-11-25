/**
 * @file all_in_one.c
 * @brief Implementación de la interfaz gráfica de smartwatch (240×240 redonda) con LVGL 9.x.
 *
 * Estructura de vistas dentro del contenedor raíz:
 *   - **ALL**   : dial con círculo interior + textos HR/SpO2 + arco + chart PPG reducido + agujas.
 *   - **CLOCK** : dial sin círculo interior + agujas (reloj a pantalla completa).
 *   - **PARAMS**: anillo animado de latido + íconos + HR + SpO2 + chart PPG ampliado.
 *
 * Solo una vista es visible a la vez; las demás se ocultan con LV_OBJ_FLAG_HIDDEN
 * para que LVGL no las redibuje.
 *
 * @author Amilcar Muñoz (amilcar.munioz@gmail.com)
 */
#include "all_in_one.h"

#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ */
/* Fuente grande de la vista PARAMS                                    */
/* ------------------------------------------------------------------ */
#ifndef ALL_IN_ONE_BIG_FONT
#define ALL_IN_ONE_BIG_FONT (&lv_font_montserrat_32)
#endif

/** @name Geometría del display
 *  @brief Constantes geométricas para la pantalla circular de 240×240 px.
 *  @{
 */
#define WF_SIZE         240     /*!< Tamaño del watchface en píxeles */
#define WF_CX           120     /*!< Coordenada X del centro */
#define WF_CY           120     /*!< Coordenada Y del centro */
/** @} */

/** @name Geometría del dial
 *  @brief Radios de ticks y números del reloj analógico.
 *  @{
 */
#define R_INNER         80      /*!< Radio del círculo interior oscuro */
#define R_TICK_OUT      116     /*!< Radio exterior de los ticks */
#define R_TICK_MAJOR_IN 106     /*!< Radio interior de los ticks de hora */
#define R_TICK_MINOR_IN 111     /*!< Radio interior de los ticks de minuto */
#define R_NUMBERS       92      /*!< Radio donde se centran los números 1-12 */
/** @} */

/** @name Dimensiones de las agujas
 *  @{
 */
#define HOUR_LEN        50      /*!< Largo de la aguja de hora */
#define HOUR_TAIL       8       /*!< Cola de la aguja de hora */
#define HOUR_HALF_W     4.0f    /*!< Medio ancho de la aguja de hora */
#define MIN_LEN         78      /*!< Largo de la aguja de minutos */
#define MIN_TAIL        8       /*!< Cola de la aguja de minutos */
#define MIN_HALF_W      3.5f    /*!< Medio ancho de la aguja de minutos */
#define SEC_LEN         96      /*!< Largo del segundero (parte delantera) */
#define SEC_TAIL        40      /*!< Largo del segundero (parte trasera) */
/** @} */

/** @name Configuración de los charts PPG
 *  @{
 */
#define PPG_POINTS_ALL      300 /*!< Puntos del chart PPG en la vista ALL */
#define PPG_POINTS_PARAMS   350 /*!< Puntos del chart PPG en la vista PARAMS */
/** @} */

/** @name Configuración del anillo de latido (vista PARAMS)
 *  @{
 */
#define RING_D_MIN          216 /*!< Diámetro inicial del anillo */
#define RING_D_MAX          236 /*!< Diámetro final del anillo */
#define RING_W              5   /*!< Grosor del anillo en píxeles */
#define RING_OPA_START      200 /*!< Opacidad inicial del anillo (se desvanece a 0) */
#define RING_ANIM_MAX_MS    700 /*!< Duración máxima de la animación en ms */
#define RING_ANIM_MIN_MS    300 /*!< Duración mínima de la animación en ms */
/** @} */

/** @name Índices de widgets duplicados entre vistas
 *  @{
 */
#define IDX_ALL             0   /*!< Índice para la vista ALL */
#define IDX_PARAMS          1   /*!< Índice para la vista PARAMS */
/** @} */

/** @name Paleta de colores (RGB hexadecimal)
 *  @{
 */
#define C_BG            0x05080A    /*!< Fondo del watchface */
#define C_INNER         0x141A1E    /*!< Relleno del círculo interior */
#define C_INNER_BORDER  0x232B30    /*!< Borde del círculo interior */
#define C_TICK_MAJOR    0x4DE8F5    /*!< Ticks de hora */
#define C_TICK_MINOR    0x2A8C9A    /*!< Ticks de minuto */
#define C_NUMBER        0xFFFFFF    /*!< Números 1-12 */
#define C_HAND          0xC9D3D6    /*!< Agujas de hora y minuto */
#define C_SEC_FRONT     0x3AA0FF    /*!< Segundero (parte delantera) */
#define C_SEC_TAIL      0xFF4545    /*!< Segundero (cola) */
#define C_HEART         0xF0504B    /*!< Ícono de corazón */
#define C_BUBBLE        0x3AA0FF    /*!< Ícono de burbuja (SpO2) */
#define C_PPG           0x3CFF6E    /*!< Curva PPG */
#define C_TEXT_DIM      0xA9B4B8    /*!< Texto secundario ("BPM", "SpO2") */
#define C_RING          0x4FC3F7    /*!< Anillo de latido */
/** @} */

/**
 * @brief Configuración del dial del reloj analógico.
 *
 * Permite parametrizar las diferencias entre el dial de la vista ALL
 * (con círculo interior e íconos) y el de la vista CLOCK (sin ellos).
 */
typedef struct {
    bool inner_circle;          /*!< Dibuja el círculo interior oscuro */
    bool icons;                 /*!< Dibuja los íconos de corazón y burbuja */
    const lv_font_t *num_font;  /*!< Fuente de los números 1-12 */
} dial_cfg_t;

static const dial_cfg_t s_dial_all   = { true,  true,  &lv_font_montserrat_14 };   /*!< Dial de la vista ALL */
static const dial_cfg_t s_dial_clock = { false, false, &lv_font_montserrat_20 };   /*!< Dial de la vista CLOCK */

/** @name Estado interno del módulo
 *  @{
 */
static lv_obj_t *s_root;                               /*!< Contenedor raíz de 240×240 */
static lv_obj_t *s_view[ALL_IN_ONE_VIEW_COUNT];         /*!< Contenedores de cada vista */
static all_in_one_view_t s_cur = ALL_IN_ONE_VIEW_ALL;   /*!< Vista actualmente visible */

static lv_obj_t *s_hands_all;      /*!< Layer de dibujo de agujas en la vista ALL */
static lv_obj_t *s_hands_clock;    /*!< Layer de dibujo de agujas en la vista CLOCK */

static lv_obj_t *s_lbl_hr[2];     /*!< Labels de HR [IDX_ALL, IDX_PARAMS] */
static lv_obj_t *s_lbl_spo2[2];   /*!< Labels de SpO2 [IDX_ALL, IDX_PARAMS] */
static lv_obj_t *s_chart[2];      /*!< Charts PPG [IDX_ALL, IDX_PARAMS] */
static lv_chart_series_t *s_ser[2]; /*!< Series de datos de cada chart */

static lv_obj_t *s_ring;           /*!< Widget del anillo de latido */
static lv_timer_t *s_beat_timer;   /*!< Timer para latido automático */
static bool s_beat_auto;           /*!< Flag de modo automático de latido */
static bool s_hr_valid;            /*!< Flag de HR válido para el anillo */
static uint32_t s_beat_ms = 833;   /*!< Período de un latido en ms (72 BPM por defecto) */

static uint8_t s_h = 10, s_m = 10, s_s = 30;  /*!< Hora actual del reloj (HH:MM:SS) */

static float s_sin[60];    /*!< Tabla de senos precalculados cada 6° */
static float s_cos[60];    /*!< Tabla de cosenos precalculados cada 6° */
/** @} */

static const char *const s_hour_txt[12] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"
};

/* ------------------------------------------------------------------ */
/* Helpers de dibujo                                                   */
/* ------------------------------------------------------------------ */
static void draw_line(lv_layer_t *layer, lv_draw_line_dsc_t *d,
                      float x1, float y1, float x2, float y2)
{
    d->p1.x = (lv_value_precise_t)x1;
    d->p1.y = (lv_value_precise_t)y1;
    d->p2.x = (lv_value_precise_t)x2;
    d->p2.y = (lv_value_precise_t)y2;
    lv_draw_line(layer, d);
}

static void draw_circle(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                        lv_color_t fill, lv_color_t border, int32_t border_w)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = fill;
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    d.border_width = border_w;
    d.border_color = border;
    d.border_opa = LV_OPA_COVER;
    lv_area_t a = { x1, y1, x2, y2 };
    lv_draw_rect(layer, &d, &a);
}

static void draw_tri(lv_layer_t *layer, lv_color_t col,
                     int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    t.color = col;
    t.opa = LV_OPA_COVER;
    t.p[0].x = x0; t.p[0].y = y0;
    t.p[1].x = x1; t.p[1].y = y1;
    t.p[2].x = x2; t.p[2].y = y2;
    lv_draw_triangle(layer, &t);
}

static void draw_heart(lv_layer_t *layer, int32_t cx, int32_t cy, lv_color_t col)
{
    draw_circle(layer, cx - 7, cy - 6, cx - 1, cy, col, col, 0);
    draw_circle(layer, cx,     cy - 6, cx + 6, cy, col, col, 0);
    draw_tri(layer, col, cx - 7, cy - 2, cx + 7, cy - 2, cx, cy + 8);
}

/** Burbuja: círculo con borde y relleno translúcido, un brillo y una burbujita chica. */
static void draw_bubble(lv_layer_t *layer, int32_t cx, int32_t cy, lv_color_t col)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = col;
    d.bg_opa = LV_OPA_20;
    d.border_color = col;
    d.border_width = 2;
    d.border_opa = LV_OPA_COVER;

    /* Burbuja principal (16 px) */
    lv_area_t big = { cx - 8, cy - 7, cx + 7, cy + 8 };
    lv_draw_rect(layer, &d, &big);

    /* Burbuja chica (6 px) arriba a la derecha */
    d.border_width = 1;
    lv_area_t small = { cx + 6, cy - 12, cx + 11, cy - 7 };
    lv_draw_rect(layer, &d, &small);

    /* Brillo dentro de la burbuja principal */
    draw_circle(layer, cx - 5, cy - 4, cx - 3, cy - 2,
                lv_color_white(), lv_color_white(), 0);
}

/**
 * Aguja hueca: contorno rectangular con puntas redondeadas.
 * El interior queda transparente. deg: 0 = 12 en punto, sentido horario.
 */
static void draw_hollow_hand(lv_layer_t *layer, int32_t cx, int32_t cy, float deg,
                             int len, int tail, float hw, lv_color_t col)
{
    float rad = deg * (float)M_PI / 180.0f;
    float dx = sinf(rad);
    float dy = -cosf(rad);
    float px = -dy;     /* perpendicular */
    float py = dx;

    float ax = cx - dx * tail - px * hw, ay = cy - dy * tail - py * hw;
    float bx = cx - dx * tail + px * hw, by = cy - dy * tail + py * hw;
    float qx = cx + dx * len  + px * hw, qy = cy + dy * len  + py * hw;
    float rx = cx + dx * len  - px * hw, ry = cy + dy * len  - py * hw;

    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = col;
    d.width = 2;
    d.opa = LV_OPA_COVER;
    d.round_start = 1;
    d.round_end = 1;

    draw_line(layer, &d, ax, ay, bx, by);
    draw_line(layer, &d, bx, by, qx, qy);
    draw_line(layer, &d, qx, qy, rx, ry);
    draw_line(layer, &d, rx, ry, ax, ay);
}

/* ------------------------------------------------------------------ */
/* Callbacks de dibujo custom                                          */
/* ------------------------------------------------------------------ */

/** Dial: círculo interior (opcional), ticks, números 1-12 e íconos (opcionales). */
static void dial_draw_cb(lv_event_t *e)
{
    const dial_cfg_t *cfg = (const dial_cfg_t *)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const int32_t cx = coords.x1 + WF_CX;
    const int32_t cy = coords.y1 + WF_CY;

    /* Círculo interior */
    if(cfg->inner_circle) {
        draw_circle(layer, cx - R_INNER, cy - R_INNER, cx + R_INNER - 1, cy + R_INNER - 1,
                    lv_color_hex(C_INNER), lv_color_hex(C_INNER_BORDER), 1);
    }

    /* Ticks */
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.round_start = 1;
    ld.round_end = 1;
    ld.opa = LV_OPA_COVER;
    for(int i = 0; i < 60; i++) {
        bool major = (i % 5) == 0;
        int r_in = major ? R_TICK_MAJOR_IN : R_TICK_MINOR_IN;
        ld.color = lv_color_hex(major ? C_TICK_MAJOR : C_TICK_MINOR);
        ld.width = major ? 3 : 1;
        draw_line(layer, &ld,
                  cx + r_in * s_sin[i],       cy - r_in * s_cos[i],
                  cx + R_TICK_OUT * s_sin[i], cy - R_TICK_OUT * s_cos[i]);
    }

    /* Números 1..12 */
    lv_draw_label_dsc_t td;
    lv_draw_label_dsc_init(&td);
    td.font = cfg->num_font;
    td.color = lv_color_hex(C_NUMBER);
    td.opa = LV_OPA_COVER;
    td.align = LV_TEXT_ALIGN_CENTER;
    const int32_t fh = lv_font_get_line_height(td.font);
    for(int h = 1; h <= 12; h++) {
        int idx = (h % 12) * 5;
        int32_t px = cx + (int32_t)lroundf(R_NUMBERS * s_sin[idx]);
        int32_t py = cy - (int32_t)lroundf(R_NUMBERS * s_cos[idx]);
        td.text = s_hour_txt[h - 1];
        lv_area_t ta = { px - 14, py - fh / 2, px + 13, py - fh / 2 + fh - 1 };
        lv_draw_label(layer, &td, &ta);
    }

    /* Íconos: corazón (HR) y burbuja (SpO2) de la vista ALL */
    if(cfg->icons) {
        draw_heart(layer, cx - 50, cy - 14, lv_color_hex(C_HEART));
        draw_bubble(layer, cx + 30, cy - 38, lv_color_hex(C_BUBBLE));
    }
}

/** Íconos de la vista PARAMS. */
static void params_icons_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const int32_t cx = coords.x1 + WF_CX;
    const int32_t cy = coords.y1 + WF_CY;

    draw_heart(layer, cx - 50, cy - 70, lv_color_hex(C_HEART));
    draw_bubble(layer, cx + 50, cy - 70, lv_color_hex(C_BUBBLE));
}

/** Agujas (se usa en la vista ALL y en la CLOCK). */
static void hands_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_obj_t *obj = lv_event_get_target_obj(e);

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    const int32_t cx = coords.x1 + WF_CX;
    const int32_t cy = coords.y1 + WF_CY;

    float hour_deg = (float)(s_h % 12) * 30.0f + (float)s_m * 0.5f;
    float min_deg  = (float)s_m * 6.0f + (float)s_s * 0.1f;
    float sec_deg  = (float)s_s * 6.0f;

    lv_color_t hand = lv_color_hex(C_HAND);
    draw_hollow_hand(layer, cx, cy, hour_deg, HOUR_LEN, HOUR_TAIL, HOUR_HALF_W, hand);
    draw_hollow_hand(layer, cx, cy, min_deg,  MIN_LEN,  MIN_TAIL,  MIN_HALF_W,  hand);

    /* Segundero: frente azul, cola roja */
    float rad = sec_deg * (float)M_PI / 180.0f;
    float dx = sinf(rad), dy = -cosf(rad);
    lv_draw_line_dsc_t sd;
    lv_draw_line_dsc_init(&sd);
    sd.width = 1;
    sd.opa = LV_OPA_COVER;
    sd.round_start = 1;
    sd.round_end = 1;

    sd.color = lv_color_hex(C_SEC_FRONT);
    draw_line(layer, &sd, cx, cy, cx + dx * SEC_LEN, cy + dy * SEC_LEN);
    sd.color = lv_color_hex(C_SEC_TAIL);
    draw_line(layer, &sd, cx, cy, cx - dx * SEC_TAIL, cy - dy * SEC_TAIL);

    /* Eje central */
    draw_circle(layer, cx - 6, cy - 6, cx + 5, cy + 5,
                lv_color_hex(0x0A1014), lv_color_hex(C_SEC_FRONT), 2);
}

/* ------------------------------------------------------------------ */
/* Helpers de widgets                                                  */
/* ------------------------------------------------------------------ */
static lv_obj_t *make_view(lv_obj_t *parent)
{
    lv_obj_t *v = lv_obj_create(parent);
    lv_obj_remove_style_all(v);
    lv_obj_set_size(v, WF_SIZE, WF_SIZE);
    lv_obj_set_pos(v, 0, 0);
    lv_obj_set_style_bg_color(v, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
    lv_obj_remove_flag(v, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(v, LV_OBJ_FLAG_CLICKABLE);
    return v;
}

static lv_obj_t *make_draw_layer(lv_obj_t *parent, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, WF_SIZE, WF_SIZE);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(o, cb, LV_EVENT_DRAW_MAIN, user_data);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            const char *txt, int32_t dx, int32_t dy)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, txt);
    lv_obj_align(l, LV_ALIGN_CENTER, dx, dy);
    return l;
}

static void make_ppg_chart(int idx, lv_obj_t *parent, int32_t w, int32_t h, int32_t dy, int points)
{
    lv_obj_t *c = lv_chart_create(parent);
    s_chart[idx] = c;

    lv_obj_set_size(c, w, h);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, dy);
    lv_chart_set_type(c, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, points);
    lv_chart_set_update_mode(c, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    lv_chart_set_div_line_count(c, 0, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_radius(c, 0, 0);
    lv_obj_set_style_size(c, 0, 0, LV_PART_INDICATOR);          /* sin puntos */
    lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);

    s_ser[idx] = lv_chart_add_series(c, lv_color_hex(C_PPG), LV_CHART_AXIS_PRIMARY_Y);
    for(int i = 0; i < points; i++) lv_chart_set_next_value(c, s_ser[idx], 250);
}

/* ------------------------------------------------------------------ */
/* Anillo de latido                                                    */
/* ------------------------------------------------------------------ */
static void ring_anim_cb(void *var, int32_t v)      /* v: 0..100 */
{
    lv_obj_t *ring = (lv_obj_t *)var;
    int32_t d = RING_D_MIN + ((RING_D_MAX - RING_D_MIN) * v) / 100;
    lv_obj_set_size(ring, d, d);
    lv_obj_center(ring);
    lv_obj_set_style_border_opa(ring, (lv_opa_t)(((100 - v) * RING_OPA_START) / 100), 0);
}

static void ring_done_cb(lv_anim_t *a)
{
    lv_obj_add_flag((lv_obj_t *)a->var, LV_OBJ_FLAG_HIDDEN);
}

static void ring_stop(void)
{
    if(!s_ring) return;
    lv_anim_delete(s_ring, ring_anim_cb);
    lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
}

static void beat_timer_refresh(void)
{
    if(!s_beat_timer) return;
    if(s_beat_auto && s_hr_valid) lv_timer_resume(s_beat_timer);
    else lv_timer_pause(s_beat_timer);
}

static void beat_timer_cb(lv_timer_t *t)
{
    (void)t;
    all_in_one_beat();
}

/* ------------------------------------------------------------------ */
/* Construcción de las vistas                                          */
/* ------------------------------------------------------------------ */
static void build_view_all(lv_obj_t *v)
{
    make_draw_layer(v, dial_draw_cb, (void *)&s_dial_all);

    /* Textos (posiciones relativas al centro de la pantalla) */
    s_lbl_hr[IDX_ALL] = make_label(v, &lv_font_montserrat_20, 0xFFFFFF, "--", -46, 5);
    make_label(v, &lv_font_montserrat_12, C_TEXT_DIM, "BPM", -44, 22);
    s_lbl_spo2[IDX_ALL] = make_label(v, &lv_font_montserrat_20, 0xFFFFFF, "--", 42, -18);
    make_label(v, &lv_font_montserrat_12, C_TEXT_DIM, "SpO2", 42, -2);
    make_label(v, &lv_font_montserrat_12, C_TEXT_DIM, "PPG", 4, 31);

    /* Arco verde inferior */
    lv_obj_t *arc = lv_arc_create(v);
    lv_obj_set_size(arc, 150, 150);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 30, 150);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 100);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(C_PPG), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    /* Chart PPG chico */
    make_ppg_chart(IDX_ALL, v, 80, 24, 51, PPG_POINTS_ALL);

    /* Agujas (siempre arriba de todo) */
    s_hands_all = make_draw_layer(v, hands_draw_cb, NULL);
}

static void build_view_clock(lv_obj_t *v)
{
    make_draw_layer(v, dial_draw_cb, (void *)&s_dial_clock);
    s_hands_clock = make_draw_layer(v, hands_draw_cb, NULL);
}

static void build_view_params(lv_obj_t *v)
{
    /* Anillo: se crea primero para quedar detrás de todo lo demás */
    s_ring = lv_obj_create(v);
    lv_obj_remove_style_all(s_ring);
    lv_obj_set_size(s_ring, RING_D_MIN, RING_D_MIN);
    lv_obj_center(s_ring);
    lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ring, RING_W, 0);
    lv_obj_set_style_border_color(s_ring, lv_color_hex(C_RING), 0);
    lv_obj_set_style_border_opa(s_ring, RING_OPA_START, 0);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ring, LV_OBJ_FLAG_HIDDEN);

    /* Íconos */
    make_draw_layer(v, params_icons_cb, NULL);

    /* HR a la izquierda, SpO2 a la derecha */
    s_lbl_hr[IDX_PARAMS] = make_label(v, ALL_IN_ONE_BIG_FONT, 0xFFFFFF, "--", -50, -38);
    make_label(v, &lv_font_montserrat_12, C_TEXT_DIM, "BPM", -50, -12);
    s_lbl_spo2[IDX_PARAMS] = make_label(v, ALL_IN_ONE_BIG_FONT, 0xFFFFFF, "--", 50, -38);
    make_label(v, &lv_font_montserrat_12, C_TEXT_DIM, "SpO2", 50, -12);

    /* Chart PPG grande */
    make_ppg_chart(IDX_PARAMS, v, 150, 76, 50, PPG_POINTS_PARAMS);
}

/* ------------------------------------------------------------------ */
/* Reloj interno                                                       */
/* ------------------------------------------------------------------ */
static void invalidate_hands(void)
{
    if(s_cur == ALL_IN_ONE_VIEW_ALL && s_hands_all) lv_obj_invalidate(s_hands_all);
    else if(s_cur == ALL_IN_ONE_VIEW_CLOCK && s_hands_clock) lv_obj_invalidate(s_hands_clock);
}

static void clock_tick_cb(lv_timer_t *t)
{
    (void)t;
    if(++s_s >= 60) {
        s_s = 0;
        if(++s_m >= 60) {
            s_m = 0;
            if(++s_h >= 24) s_h = 0;
        }
    }
    invalidate_hands();
}

/* ------------------------------------------------------------------ */
/* API pública                                                         */
/* ------------------------------------------------------------------ */
lv_obj_t *all_in_one_create(lv_obj_t *parent)
{
    if(parent == NULL) parent = lv_screen_active();

    for(int i = 0; i < 60; i++) {
        float a = (float)i * 6.0f * (float)M_PI / 180.0f;
        s_sin[i] = sinf(a);
        s_cos[i] = cosf(a);
    }

    s_root = make_view(parent);
    for(int i = 0; i < ALL_IN_ONE_VIEW_COUNT; i++) s_view[i] = make_view(s_root);

    build_view_all(s_view[ALL_IN_ONE_VIEW_ALL]);
    build_view_clock(s_view[ALL_IN_ONE_VIEW_CLOCK]);
    build_view_params(s_view[ALL_IN_ONE_VIEW_PARAMS]);

    /* Arranca en la vista ALL; las demás ocultas */
    s_cur = ALL_IN_ONE_VIEW_ALL;
    lv_obj_add_flag(s_view[ALL_IN_ONE_VIEW_CLOCK], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_view[ALL_IN_ONE_VIEW_PARAMS], LV_OBJ_FLAG_HIDDEN);

    /* Reloj interno (1 s) y temporizador de latido automático (pausado) */
    lv_timer_create(clock_tick_cb, 1000, NULL);
    s_beat_timer = lv_timer_create(beat_timer_cb, s_beat_ms, NULL);
    lv_timer_pause(s_beat_timer);

    return s_root;
}

void all_in_one_set_view(all_in_one_view_t view)
{
    if(s_root == NULL || view >= ALL_IN_ONE_VIEW_COUNT || view == s_cur) return;

    if(s_cur == ALL_IN_ONE_VIEW_PARAMS) ring_stop();

    lv_obj_add_flag(s_view[s_cur], LV_OBJ_FLAG_HIDDEN);
    s_cur = view;
    lv_obj_remove_flag(s_view[s_cur], LV_OBJ_FLAG_HIDDEN);
}

void all_in_one_next_view(void)
{
    all_in_one_set_view((all_in_one_view_t)((s_cur + 1) % ALL_IN_ONE_VIEW_COUNT));
}

all_in_one_view_t all_in_one_get_view(void)
{
    return s_cur;
}

void all_in_one_set_time(uint8_t h, uint8_t m, uint8_t s)
{
    s_h = h % 24;
    s_m = m % 60;
    s_s = s % 60;
    invalidate_hands();
}

void all_in_one_set_hr(int bpm)
{
    for(int i = 0; i < 2; i++) {
        if(!s_lbl_hr[i]) continue;
        if(bpm <= 0) lv_label_set_text(s_lbl_hr[i], "--");
        else lv_label_set_text_fmt(s_lbl_hr[i], "%d", bpm);
    }

    s_hr_valid = (bpm > 0);
    if(s_hr_valid) {
        s_beat_ms = 60000u / (uint32_t)bpm;
        if(s_beat_ms < 250) s_beat_ms = 250;
        if(s_beat_ms > 2000) s_beat_ms = 2000;
        if(s_beat_timer) lv_timer_set_period(s_beat_timer, s_beat_ms);
    }
    beat_timer_refresh();
}

void all_in_one_set_spo2(int pct)
{
    for(int i = 0; i < 2; i++) {
        if(!s_lbl_spo2[i]) continue;
        if(pct <= 0) lv_label_set_text(s_lbl_spo2[i], "--");
        else lv_label_set_text_fmt(s_lbl_spo2[i], "%d%%", pct);
    }
}

void all_in_one_ppg_push(int value_0_1000)
{
    if(value_0_1000 < 0) value_0_1000 = 0;
    if(value_0_1000 > 1000) value_0_1000 = 1000;
    for(int i = 0; i < 2; i++) {
        if(s_chart[i]) lv_chart_set_next_value(s_chart[i], s_ser[i], value_0_1000);
    }
}

void all_in_one_beat(void)
{
    if(s_ring == NULL || s_cur != ALL_IN_ONE_VIEW_PARAMS) return;

    /* La animación dura ~80% del período del latido, entre 300 y 700 ms */
    uint32_t dur = (s_beat_ms * 8u) / 10u;
    if(dur > RING_ANIM_MAX_MS) dur = RING_ANIM_MAX_MS;
    if(dur < RING_ANIM_MIN_MS) dur = RING_ANIM_MIN_MS;

    lv_anim_delete(s_ring, ring_anim_cb);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_ring);
    lv_anim_set_exec_cb(&a, ring_anim_cb);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, ring_done_cb);
    lv_anim_start(&a);
}

void all_in_one_set_beat_auto(bool enable)
{
    s_beat_auto = enable;
    beat_timer_refresh();
}

/* ------------------------------------------------------------------ */
/* Demo (sin sensor)                                                   */
/* ------------------------------------------------------------------ */
static void demo_cb(lv_timer_t *t)
{
    (void)t;
    static float phase = 0.0f;
    float prev = phase;

    phase += 0.02f / 0.83f;                 /* 50 Hz, latido de ~0.83 s (72 BPM) */
    if(phase >= 1.0f) phase -= 1.0f;

    /* El latido se dispara justo en el pico de la onda */
    if(prev < 0.20f && phase >= 0.20f) all_in_one_beat();

    float a = (phase - 0.20f) / 0.06f;
    float b = (phase - 0.45f) / 0.08f;
    float v = 0.25f + 0.65f * expf(-a * a) + 0.18f * expf(-b * b);
    all_in_one_ppg_push((int)(v * 1000.0f));
}

void all_in_one_demo_start(void)
{
    all_in_one_set_hr(72);
    all_in_one_set_spo2(98);
    lv_timer_create(demo_cb, 20, NULL);
}