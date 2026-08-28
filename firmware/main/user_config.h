#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#define LCD_WIDTH      400
#define LCD_HEIGHT     300

#define RLCD_DC_PIN    GPIO_NUM_5
#define RLCD_CS_PIN    GPIO_NUM_40
#define RLCD_SCK_PIN   GPIO_NUM_11
#define RLCD_MOSI_PIN  GPIO_NUM_12
#define RLCD_RST_PIN   GPIO_NUM_41
#define RLCD_TE_PIN    GPIO_NUM_6

#define ESP32_I2C_SDA_PIN   GPIO_NUM_13
#define ESP32_I2C_SCL_PIN   GPIO_NUM_14

#define BTN_OK_PIN          GPIO_NUM_0    /* BOOT  */
#define BTN_SELECT_PIN      GPIO_NUM_18   /* KEY   */

#endif