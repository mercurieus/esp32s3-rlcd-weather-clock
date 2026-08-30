#ifndef EEZ_LVGL_UI_IMAGES_H
#define EEZ_LVGL_UI_IMAGES_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const lv_img_dsc_t img_icon_snow;
extern const lv_img_dsc_t img_icon_rain;
extern const lv_img_dsc_t img_icon_sun;
extern const lv_img_dsc_t img_icon_cloud;
extern const lv_img_dsc_t img_icon_storm;
extern const lv_img_dsc_t img_icon_partly_cloudy;

#ifndef EXT_IMG_DESC_T
#define EXT_IMG_DESC_T
typedef struct _ext_img_desc_t {
    const char *name;
    const lv_img_dsc_t *img_dsc;
} ext_img_desc_t;
#endif

extern const ext_img_desc_t images[6];

#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_IMAGES_H*/