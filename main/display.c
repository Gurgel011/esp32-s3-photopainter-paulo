#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "esp_lcd_sh8601.h"

static const char *TAG = "display";

#define LCD_HOST SPI2_HOST
#define LCD_H_RES 466
#define LCD_V_RES 466
#define LCD_BIT_PER_PIXEL 16

#define PIN_LCD_DATA0 9
#define PIN_LCD_DATA1 10
#define PIN_LCD_DATA2 11
#define PIN_LCD_DATA3 12
#define PIN_LCD_RST 13
#define PIN_LCD_SCK 14
#define PIN_LCD_CS 15

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x36, (uint8_t[]){0xC0}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x11, (uint8_t[]){0x00}, 0, 100},
    {0x29, (uint8_t[]){0x00}, 0, 0},
};

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t color = ((uint16_t)(r & 0xf8) << 8) | ((uint16_t)(g & 0xfc) << 3) | (b >> 3);
    return (uint16_t)((color << 8) | (color >> 8));
}

static esp_err_t fill_rect(esp_lcd_panel_handle_t panel, int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return ESP_OK;
    }

    uint16_t *line = heap_caps_malloc((size_t)w * sizeof(uint16_t), MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(line, ESP_ERR_NO_MEM, TAG, "sem memoria para linha do LCD");

    for (int i = 0; i < w; i++) {
        line[i] = color;
    }

    for (int row = y; row < y + h; row++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_draw_bitmap(panel, x, row, x + w, row + 1, line),
                            TAG, "falha ao desenhar linha");
    }

    free(line);
    return ESP_OK;
}

static const uint8_t *glyph_for(char c)
{
    static const uint8_t A[] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    static const uint8_t D[] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
    static const uint8_t E[] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    static const uint8_t I[] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f};
    static const uint8_t M[] = {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t N[] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t O[] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    static const uint8_t R[] = {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    static const uint8_t S[] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    static const uint8_t T[] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t U[] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    static const uint8_t V[] = {0x11, 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04};
    static const uint8_t ONE[] = {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};

    switch (c) {
    case 'A': return A;
    case 'D': return D;
    case 'E': return E;
    case 'I': return I;
    case 'M': return M;
    case 'N': return N;
    case 'O': return O;
    case 'R': return R;
    case 'S': return S;
    case 'T': return T;
    case 'U': return U;
    case 'V': return V;
    case '1': return ONE;
    default: return NULL;
    }
}

static int text_width(const char *text, int scale)
{
    int width = 0;
    for (const char *p = text; *p; p++) {
        width += (*p == ' ') ? (3 * scale) : (6 * scale);
    }
    return width > 0 ? width - scale : 0;
}

static esp_err_t draw_text(esp_lcd_panel_handle_t panel, const char *text, int x, int y, int scale, uint16_t color)
{
    for (const char *p = text; *p; p++) {
        if (*p == ' ') {
            x += 3 * scale;
            continue;
        }

        const uint8_t *glyph = glyph_for(*p);
        if (glyph == NULL) {
            x += 6 * scale;
            continue;
        }

        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph[row] & (1 << (4 - col))) {
                    ESP_RETURN_ON_ERROR(fill_rect(panel, x + col * scale, y + row * scale, scale, scale, color),
                                        TAG, "falha ao desenhar texto");
                }
            }
        }
        x += 6 * scale;
    }

    return ESP_OK;
}

static esp_err_t draw_centered_text(esp_lcd_panel_handle_t panel, const char *text, int y, int scale, uint16_t color)
{
    const int x = (LCD_H_RES - text_width(text, scale)) / 2;
    return draw_text(panel, text, x, y, scale, color);
}

esp_err_t display_show_antena1(void)
{
    ESP_LOGI(TAG, "Inicializando AMOLED SH8601 466x466 via QSPI");

    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_LCD_SCK, PIN_LCD_DATA0, PIN_LCD_DATA1, PIN_LCD_DATA2, PIN_LCD_DATA3,
        LCD_H_RES * LCD_V_RES * sizeof(uint16_t));
    esp_err_t ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "falha ao iniciar barramento SPI/QSPI");
    }

    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, NULL, NULL);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle),
                        TAG, "falha ao criar IO do painel");

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .flags = {
            .reset_active_high = 0,
        },
        .vendor_config = &vendor_config,
    };

    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel), TAG, "falha ao criar painel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(panel, 0x08, 0x00), TAG, "falha ao configurar gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "falha no reset do painel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "falha ao inicializar painel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "falha ao ligar display");

    const uint16_t black = rgb565(3, 5, 8);
    const uint16_t red = rgb565(218, 28, 45);
    const uint16_t white = rgb565(255, 255, 255);
    const uint16_t gray = rgb565(150, 160, 172);

    ESP_RETURN_ON_ERROR(fill_rect(panel, 0, 0, LCD_H_RES, LCD_V_RES, black), TAG, "falha ao limpar tela");
    ESP_RETURN_ON_ERROR(draw_centered_text(panel, "ANTENA 1", 165, 10, white), TAG, "falha ao desenhar marca");
    ESP_RETURN_ON_ERROR(fill_rect(panel, 96, 260, 274, 6, red), TAG, "falha ao desenhar linha");
    ESP_RETURN_ON_ERROR(draw_centered_text(panel, "A RADIO MAIS OUVIDA", 304, 4, gray), TAG, "falha ao desenhar frase");

    ESP_LOGI(TAG, "AMOLED SH8601 inicializado e tela Antena 1 desenhada");
    return ESP_OK;
}
