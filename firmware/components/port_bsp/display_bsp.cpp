#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include "display_bsp.h"

RlcdWindow RLCD_ComputeWindow(int width, int height, int x1, int y1, int x2, int y2)
{
    /* Clamp to panel bounds first - defensive against any caller passing
       out-of-range LVGL coordinates. */
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > width - 1)  x2 = width - 1;
    if (y2 > height - 1) y2 = height - 1;

    /* X (RASET): 2px/unit, no inversion. */
    uint8_t rs = (uint8_t)(x1 >> 1);
    uint8_t re = (uint8_t)(x2 >> 1);

    /* Y (CASET): 12px/unit (3 InitLandscapeLUT() block_y groups of 4 rows
       each), Y-inverted via inv_y = height-1-y, and CASET *decreases* as
       screen-Y increases (hardware-confirmed: CASET's low end measured at
       the top of the screen, where inv_y/block_y is largest). */
    int inv_y1 = height - 1 - y1;
    int inv_y2 = height - 1 - y2;
    int by_a = inv_y1 >> 2;
    int by_b = inv_y2 >> 2;
    int by_lo = (by_a < by_b) ? by_a : by_b;
    int by_hi = (by_a > by_b) ? by_a : by_b;
    int g_lo = by_lo / 3;   /* integer division already expands to the
                                enclosing 12px-aligned band */
    int g_hi = by_hi / 3;

    RlcdWindow w;
    w.caset_xs = (uint8_t)(42 - g_hi);   /* larger block_y group -> smaller CASET */
    w.caset_xe = (uint8_t)(42 - g_lo);   /* smaller block_y group -> larger CASET */
    w.raset_ys = rs;
    w.raset_ye = re;
    w.len = (int)(w.caset_xe - w.caset_xs + 1) * (int)(w.raset_ye - w.raset_ys + 1) * 3;
    return w;
}

DisplayPort::DisplayPort(int mosi, int scl, int dc, int cs, int rst, int width, int height, spi_host_device_t spihost) :
mosi_(mosi),
scl_(scl),
dc_(dc),
cs_(cs),
rst_(rst),
width_(width),
height_(height)
{
    esp_err_t        ret;
    spi_bus_config_t buscfg   = {};
    int              transfer = width_ * height_;
    buscfg.miso_io_num                   = -1;
    buscfg.mosi_io_num                   = mosi;
    buscfg.sclk_io_num                   = scl;
    buscfg.quadwp_io_num                 = -1;
    buscfg.quadhd_io_num                 = -1;
    buscfg.max_transfer_sz               = transfer;
    ret                                  = spi_bus_initialize(spihost, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = dc_;
    io_config.cs_gpio_num = cs_;
    io_config.pclk_hz = 10 * 1000 * 1000;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spihost, &io_config, &io_handle));

    xfer_done_ = xSemaphoreCreateBinary();
    assert(xfer_done_);
    xSemaphoreGive(xfer_done_);   /* nothing in flight yet */

    esp_lcd_panel_io_callbacks_t cbs = {};
    cbs.on_color_trans_done = &DisplayPort::on_color_trans_done;
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, this));

    gpio_config_t gpio_conf = {};
    gpio_conf.intr_type     = GPIO_INTR_DISABLE;
    gpio_conf.mode          = GPIO_MODE_OUTPUT;
    gpio_conf.pin_bit_mask  = (0x1ULL << rst_);
    gpio_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gpio_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));

    Set_ResetIOLevel(1);

    DisplayLen                = transfer >> 3;
    DispBuffer                = (uint8_t *) heap_caps_malloc(DisplayLen, MALLOC_CAP_SPIRAM);
    assert(DispBuffer);
    /* Persistent scratch buffer for RLCD_DisplayWindow(), sized to the
       full-panel worst case. Reused across calls rather than malloc/free
       per send - RLCD_Sendbuffera() queues an async DMA transfer and
       returns immediately, so freeing a buffer right after queuing it
       would race the still-in-flight send (this is the exact bug the
       windowed-refresh spike's own diagnostic code hit). Safe to reuse
       for the same reason DispBuffer itself already is: the next batch's
       RLCD_WaitTransferDone() (called before that batch's first pixel
       write) guarantees any previous transfer has completed before either
       buffer is touched again. */
    WindowBuffer               = (uint8_t *) heap_caps_malloc(DisplayLen, MALLOC_CAP_SPIRAM);
    assert(WindowBuffer);

#if (AlgorithmOptimization == 3)
    PixelIndexLUT = (uint16_t (*)[300])heap_caps_malloc(transfer * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    PixelBitLUT   = (uint8_t (*)[300])heap_caps_malloc(transfer * sizeof(uint8_t), MALLOC_CAP_SPIRAM);
    assert(PixelIndexLUT);
    assert(PixelBitLUT);
    if (width_ == 400) {
        InitLandscapeLUT();
    } else {
        InitPortraitLUT();
    }
#endif
}

DisplayPort::~DisplayPort() {
}

void DisplayPort::RLCD_Init() {
    RLCD_Reset();

    RLCD_SendCommand(0xD6);
    RLCD_SendData(0x17);
    RLCD_SendData(0x02);

    RLCD_SendCommand(0xD1);
    RLCD_SendData(0x01);

    RLCD_SendCommand(0xC0);
    RLCD_SendData(0x11);
    RLCD_SendData(0x04);

    RLCD_SendCommand(0xC1);
    RLCD_SendData(0x69);
    RLCD_SendData(0x69);
    RLCD_SendData(0x69);
    RLCD_SendData(0x69);

    RLCD_SendCommand(0xC2);
    RLCD_SendData(0x19);
    RLCD_SendData(0x19);
    RLCD_SendData(0x19);
    RLCD_SendData(0x19);

    RLCD_SendCommand(0xC4);
    RLCD_SendData(0x4B);
    RLCD_SendData(0x4B);
    RLCD_SendData(0x4B);
    RLCD_SendData(0x4B);

    RLCD_SendCommand(0xC5);
    RLCD_SendData(0x19);
    RLCD_SendData(0x19);
    RLCD_SendData(0x19);
    RLCD_SendData(0x19);

    RLCD_SendCommand(0xD8);
    RLCD_SendData(0x80);
    RLCD_SendData(0xE9);

    RLCD_SendCommand(0xB2);
    RLCD_SendData(0x02);

    RLCD_SendCommand(0xB3);
    RLCD_SendData(0xE5);
    RLCD_SendData(0xF6);
    RLCD_SendData(0x05);
    RLCD_SendData(0x46);
    RLCD_SendData(0x77);
    RLCD_SendData(0x77);
    RLCD_SendData(0x77);
    RLCD_SendData(0x77);
    RLCD_SendData(0x76);
    RLCD_SendData(0x45);

    RLCD_SendCommand(0xB4);
    RLCD_SendData(0x05);
    RLCD_SendData(0x46);
    RLCD_SendData(0x77);
    RLCD_SendData(0x77);
    RLCD_SendData(0x77);
    RLCD_SendData(0x77);
    RLCD_SendData(0x76);
    RLCD_SendData(0x45);

    RLCD_SendCommand(0x62);
    RLCD_SendData(0x32);
    RLCD_SendData(0x03);
    RLCD_SendData(0x1F);

    RLCD_SendCommand(0xB7);
    RLCD_SendData(0x13);

    RLCD_SendCommand(0xB0);
    RLCD_SendData(0x64);

    RLCD_SendCommand(0x11);
    vTaskDelay(pdMS_TO_TICKS(200));
    RLCD_SendCommand(0xC9);
    RLCD_SendData(0x00);

    RLCD_SendCommand(0x36);
    RLCD_SendData(0x48);

    RLCD_SendCommand(0x3A);
    RLCD_SendData(0x11);

    RLCD_SendCommand(0xB9);
    RLCD_SendData(0x20);

    RLCD_SendCommand(0xB8);
    RLCD_SendData(0x29);

    RLCD_SendCommand(0x21);

    RLCD_SendCommand(0x2A);
    RLCD_SendData(0x12);
    RLCD_SendData(0x2A);

    RLCD_SendCommand(0x2B);
    RLCD_SendData(0x00);
    RLCD_SendData(0xC7);

    RLCD_SendCommand(0x35);
    RLCD_SendData(0x00);

    RLCD_SendCommand(0xD0);
    RLCD_SendData(0xFF);

    RLCD_SendCommand(0x38);
    RLCD_SendCommand(0x29);

    /* Tried staying in HPM (~25.5Hz) instead of dropping to LPM (1Hz)
       here, hoping the slow visible "roll" on large-area updates was
       the FRCTRL/0xB2 frame rate. Hardware-measured: no change - same
       ~50px/sec roll in both HPM and LPM. That rules out the frame
       rate register; the roll is the panel's own liquid-crystal optical
       settling time, not an addressing/clock-rate limit, so there's no
       reason to pay LPM's power cost for zero visible benefit. Back to
       LPM. See docs/superpowers/specs/2026-08-29-calclock-vista-redraw-design.md
       and the SDD ledger for the full investigation. */
    RLCD_SendCommand(0x39);
    vTaskDelay(pdMS_TO_TICKS(100));

    RLCD_ColorClear(ColorWhite);
}

void DisplayPort::RLCD_ColorClear(uint8_t color) {
    memset(DispBuffer, color, DisplayLen);
}

/* Always ships the whole packed panel buffer, fixed to the full-screen
   column/row window - it does not know or care which pixels actually
   changed. Windowing this to an arbitrary sub-rectangle would need the
   panel's column/row addressing worked out against its packing (see
   RLCD_SetLandscapePixel), which isn't documented anywhere in this repo;
   left as a full send rather than guess at that on real hardware. */
void DisplayPort::RLCD_Display() {
    RLCD_SendCommand(0x2A);
    RLCD_SendData(0x12);
    RLCD_SendData(0x2A);

    RLCD_SendCommand(0x2B);
    RLCD_SendData(0x00);
    RLCD_SendData(0xC7);

    RLCD_SendCommand(0x2c);

    RLCD_Sendbuffera(DispBuffer, DisplayLen);
}

/* Sends a windowed sub-rectangle instead of the full panel. x1/y1/x2/y2 are
   inclusive screen-space coordinates; RLCD_ComputeWindow() rounds them to a
   valid CASET/RASET window and the matching payload length - see that
   function for the addressing math. Writes into the persistent
   WindowBuffer (not a fresh allocation - see the constructor) so it needs
   no wait of its own: like RLCD_Display(), it queues an async send and
   relies on the caller's existing wait/send discipline (Task 1's
   RLCD_WaitTransferDone(), called at the start of the next batch) before
   either buffer is touched again. */
void DisplayPort::RLCD_DisplayWindow(int x1, int y1, int x2, int y2) {
    RlcdWindow w = RLCD_ComputeWindow(width_, height_, x1, y1, x2, y2);

    int H4 = height_ >> 2;
    int by_start = (42 - w.caset_xe) * 3;
    int run_len  = (w.caset_xe - w.caset_xs + 1) * 3;
    int cursor = 0;
    for (int bx = w.raset_ys; bx <= w.raset_ye; bx++) {
        memcpy(WindowBuffer + cursor, &DispBuffer[bx * H4 + by_start], run_len);
        cursor += run_len;
    }

    RLCD_SendCommand(0x2A);
    RLCD_SendData(w.caset_xs);
    RLCD_SendData(w.caset_xe);

    RLCD_SendCommand(0x2B);
    RLCD_SendData(w.raset_ys);
    RLCD_SendData(w.raset_ye);

    RLCD_SendCommand(0x2c);
    RLCD_Sendbuffera(WindowBuffer, w.len);
}

void DisplayPort::RLCD_Reset(void) {
    Set_ResetIOLevel(1);
    vTaskDelay(pdMS_TO_TICKS(50));
    Set_ResetIOLevel(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    Set_ResetIOLevel(1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void DisplayPort::RLCD_SendCommand(uint8_t Reg) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle, Reg, NULL, 0));
}

void DisplayPort::RLCD_SendData(uint8_t Data) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle, -1, &Data, 1));
}

void DisplayPort::RLCD_Sendbuffera(uint8_t *Data, int len) {
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io_handle, -1, Data, len));
}

bool DisplayPort::on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                      esp_lcd_panel_io_event_data_t *edata,
                                      void *user_ctx)
{
    DisplayPort *self = static_cast<DisplayPort *>(user_ctx);
    BaseType_t high_task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(self->xfer_done_, &high_task_awoken);
    return high_task_awoken == pdTRUE;
}

void DisplayPort::RLCD_WaitTransferDone()
{
    xSemaphoreTake(xfer_done_, portMAX_DELAY);
}

void DisplayPort::Set_ResetIOLevel(uint8_t level) {
    gpio_set_level((gpio_num_t) rst_, level ? 1 : 0);
}

#if (AlgorithmOptimization != 3)

void DisplayPort::RLCD_SetPortraitPixel(uint16_t x, uint16_t y, uint8_t color) {
    if ((x >= width_) || (y >= height_)) {
        ESP_LOGE("Pixel", "Beyond the limit : (%d,%d)", x, y);
        return;
    }
#if (AlgorithmOptimization == 2)
    const uint16_t W4 = width_ >> 2;

    uint16_t byte_x = x >> 2;
    uint16_t byte_y = y >> 1;

    uint32_t index = byte_y * W4 + byte_x;

    uint8_t local_x = x & 0x03;
    uint8_t local_y = y & 0x01;

    uint8_t bit = 7 - ((local_x << 1) | local_y);

    uint8_t mask = 1 << bit;

    if (color)
        DispBuffer[index] |= mask;
    else
        DispBuffer[index] &= ~mask;
#else
    uint16_t byte_x = x / 4;
    uint16_t byte_y = y / 2;

    uint32_t index = byte_y * (width_ / 4) + byte_x;

    uint8_t local_x = x % 4;
    uint8_t local_y = y % 2;
    uint8_t bit = 7 - (local_x * 2 + local_y);
    if (color)
        DispBuffer[index] |=  (1 << bit);
    else
        DispBuffer[index] &= ~(1 << bit);
#endif
}

void DisplayPort::RLCD_SetLandscapePixel(uint16_t x, uint16_t y, uint8_t color) {
    if (x >= width_ || y >= height_)
        return;
#if (AlgorithmOptimization == 2)
    uint16_t inv_y = (height_ - 1 - y);
    const uint16_t H4 = height_ >> 2;
    uint16_t byte_x = x >> 1;
    uint16_t block_y = inv_y >> 2;
    uint32_t index = byte_x * H4 + block_y;
    uint8_t local_x = x & 0x01;
    uint8_t local_y = inv_y & 0x03;
    uint8_t bit = 7 - ((local_y << 1) | local_x);
    uint8_t mask = 1 << bit;
    if (color)
        DispBuffer[index] |= mask;
    else
        DispBuffer[index] &= ~mask;
#else
    uint16_t inv_y = height_ - 1 - y;

    uint16_t byte_x  = x / 2;
    uint16_t block_y = inv_y / 4;

    uint32_t index = byte_x * (height_ / 4) + block_y;

    uint8_t local_x = x % 2;
    uint8_t local_y = inv_y % 4;

    uint8_t bit = 7 - (local_y * 2 + local_x);

    if (color)
        DispBuffer[index] |= (1 << bit);
    else
        DispBuffer[index] &= ~(1 << bit);
#endif
}

#endif

#if (AlgorithmOptimization == 3)

void DisplayPort::InitPortraitLUT() {
    uint16_t W4 = width_ >> 2;
    for (uint16_t y = 0; y < height_; y++)
    {
        uint16_t byte_y = y >> 1;
        uint8_t  local_y = y & 1;

        for (uint16_t x = 0; x < width_; x++)
        {
            uint16_t byte_x = x >> 2;
            uint8_t  local_x = x & 3;

            uint32_t index = byte_y * W4 + byte_x;
            uint8_t bit = 7 - ((local_x << 1) | local_y);

            PixelIndexLUT[x][y] = index;
            PixelBitLUT  [x][y] = (1 << bit);
        }
    }
}

void DisplayPort::InitLandscapeLUT() {
    uint16_t H4 = height_ >> 2;

    for (uint16_t y = 0; y < height_; y++)
    {
        uint16_t inv_y = height_ - 1 - y;
        uint16_t block_y = inv_y >> 2;
        uint8_t  local_y  = inv_y & 3;

        for (uint16_t x = 0; x < width_; x++)
        {
            uint16_t byte_x = x >> 1;
            uint8_t  local_x = x & 1;

            uint32_t index = byte_x * H4 + block_y;
            uint8_t bit = 7 - ((local_y << 1) | local_x);

            PixelIndexLUT[x][y] = index;
            PixelBitLUT  [x][y] = (1 << bit);
        }
    }
}

void DisplayPort::RLCD_SetPixel(uint16_t x, uint16_t y, uint8_t color) {
    uint32_t idx = PixelIndexLUT[x][y];
    uint8_t  mask = PixelBitLUT[x][y];

    uint8_t *p = &DispBuffer[idx];

    if (color)
        *p |= mask;
    else
        *p &= ~mask;
}

#endif