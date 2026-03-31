#ifndef ROTARY_H
#define ROTARY_H

#include "esp_err.h"

// 函数声明
esp_err_t rotary_init(void);
int rotary_get_count(void);
bool rotary_is_pressed(void);

#endif // ROTARY_H