#ifndef CAM_MODULE_H
#define CAM_MODULE_H

#include "esp_camera.h"

/* 接线：SCCB SDA=4 SCL=5, VSYNC=6 HREF=7 PCLK=13, PWDN=47
 *       D0~D7 = 11,9,8,10,12,18,17,16   XCLK/RST 悬空（板载晶振） */
esp_err_t cam_module_init(void);
camera_fb_t *cam_module_grab(void);
void cam_module_release(camera_fb_t *fb);
void cam_dump_info(void);

#endif
