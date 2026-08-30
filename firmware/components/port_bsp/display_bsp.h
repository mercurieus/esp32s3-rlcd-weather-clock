#pragma once

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define AlgorithmOptimization  3

enum ColorSelection {
    ColorBlack = 0,
    ColorWhite = 0xff
};

/* A windowed CASET/RASET/payload-length triple, computed by
   RLCD_ComputeWindow() from a screen-space rectangle. Both the SPI command
   bytes and the payload-extraction loop that use this are driven from one
   RlcdWindow value so they can never compute mismatched sizes - an
   undersized RAMWR payload does not fail gracefully on this panel, it
   leaves visible garbage (hardware-confirmed). See
   docs/superpowers/specs/2026-08-30-windowed-partial-refresh-design.md. */
struct RlcdWindow {
    uint8_t caset_xs;
    uint8_t caset_xe;
    uint8_t raset_ys;
    uint8_t raset_ye;
    int     len;
};

/* Pure function (no DisplayPort instance needed) so it's directly unit
   testable - see display_bsp_test.cpp. width/height are the panel's pixel
   dimensions (400/300 on this hardware). x1/y1/x2/y2 is an inclusive
   screen-space rectangle; out-of-range values are clamped internally to
   [0,width-1]x[0,height-1]. Always expands the rect to the nearest valid
   CASET/RASET-addressable window (12px vertical granularity, 2px
   horizontal), never shrinks it - so a computed window may cover a few
   extra pixels beyond what was asked for, never fewer. */
RlcdWindow RLCD_ComputeWindow(int width, int height, int x1, int y1, int x2, int y2);

void DisplayBsp_RunTests(void);

class DisplayPort {
  private:
    esp_lcd_panel_io_handle_t io_handle = NULL;
    uint32_t            i2c_data_pdMS_TICKS = 0;
    uint32_t            i2c_done_pdMS_TICKS = 0;
    const char         *TAG                 = "Display";
    int                 mosi_;
    int                 scl_;
    int                 dc_;
    int                 cs_;
    int                 rst_;
    int                 width_;
    int                 height_;
    uint8_t            *DispBuffer = NULL;
    uint8_t            *WindowBuffer = NULL;
    int                 DisplayLen;
    SemaphoreHandle_t xfer_done_ = NULL;
    static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx);
#if (AlgorithmOptimization == 3)
    uint16_t (*PixelIndexLUT)[300];
    uint8_t  (*PixelBitLUT  )[300];
    void InitPortraitLUT();
    void InitLandscapeLUT();
#endif

    void Set_ResetIOLevel(uint8_t level);
    void RLCD_SendCommand(uint8_t Reg);
    void RLCD_SendData(uint8_t Data);
    void RLCD_Sendbuffera(uint8_t *Data, int len);
    void RLCD_Reset(void);
    /* Only RLCD_DisplayAuto() calls this - not part of the public API. */
    void RLCD_DisplayWindow(int x1, int y1, int x2, int y2);

  public:
    DisplayPort(int mosi, int scl, int dc, int cs, int rst, int width, int height, spi_host_device_t spihost = SPI3_HOST);
    ~DisplayPort();
    void RLCD_Init();
    void RLCD_ColorClear(uint8_t color);
    void RLCD_Display();
    void RLCD_DisplayAuto(int x1, int y1, int x2, int y2);
    void RLCD_WaitTransferDone();
    #if (AlgorithmOptimization != 3)
    void RLCD_SetPortraitPixel(uint16_t x, uint16_t y, uint8_t color);
    void RLCD_SetLandscapePixel(uint16_t x, uint16_t y, uint8_t color);
    #endif
    #if (AlgorithmOptimization == 3)
    void RLCD_SetPixel(uint16_t x, uint16_t y, uint8_t color);
    #endif
};