#include <stdbool.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

// Rotary编码器引脚定义
#define ROTARY_CLK_GPIO  42
#define ROTARY_DT_GPIO   17
#define ROTARY_SW_GPIO   16

// 去抖动时间（微秒）
#define DEBOUNCE_TIME_US 1000

// 旋转编码器状态
static const int8_t ENCODER_TABLE[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
static uint8_t encoder_state = 0;
static volatile int encoder_count = 0;
static volatile bool encoder_pressed = false;
static int64_t last_encoder_time = 0;
static int64_t last_button_time = 0;

static const char *TAG = "ROTARY";

static void encoder_isr_handler(void *arg)
{
    int pin = (int)arg;
    int64_t current_time = esp_timer_get_time();
    
    if (pin == ROTARY_CLK_GPIO || pin == ROTARY_DT_GPIO) {
        // 去抖动检查
        if (current_time - last_encoder_time < DEBOUNCE_TIME_US) {
            return;
        }
        last_encoder_time = current_time;
        
        // 读取当前状态
        int clk = gpio_get_level(ROTARY_CLK_GPIO);
        int dt = gpio_get_level(ROTARY_DT_GPIO);
        
        // 更新状态
        encoder_state = (encoder_state << 2) | (clk << 1) | dt;
        encoder_state &= 0x0F; // 只保留低4位
        
        // 使用状态表确定旋转方向
        int8_t delta = ENCODER_TABLE[encoder_state];
        if (delta != 0) {
            encoder_count += delta;
        }
    } else if (pin == ROTARY_SW_GPIO) {
        // 去抖动检查
        if (current_time - last_button_time < DEBOUNCE_TIME_US * 5) {
            return;
        }
        last_button_time = current_time;
        
        // 检测按键状态
        bool pressed = !gpio_get_level(ROTARY_SW_GPIO);
        encoder_pressed = pressed;
    }
}

esp_err_t rotary_init(void)
{
    // 配置Rotary编码器引脚
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ROTARY_CLK_GPIO) | (1ULL << ROTARY_DT_GPIO) | (1ULL << ROTARY_SW_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf);
    
    // 安装GPIO中断服务
    gpio_install_isr_service(0); // 使用0代替ESP_INTR_FLAG_DEFAULT
    
    // 添加中断处理函数
    gpio_isr_handler_add(ROTARY_CLK_GPIO, encoder_isr_handler, (void*)ROTARY_CLK_GPIO);
    gpio_isr_handler_add(ROTARY_DT_GPIO, encoder_isr_handler, (void*)ROTARY_DT_GPIO);
    gpio_isr_handler_add(ROTARY_SW_GPIO, encoder_isr_handler, (void*)ROTARY_SW_GPIO);
    
    // 初始化编码器状态
    int clk = gpio_get_level(ROTARY_CLK_GPIO);
    int dt = gpio_get_level(ROTARY_DT_GPIO);
    encoder_state = (clk << 1) | dt;
    encoder_count = 0;
    encoder_pressed = false;
    
    ESP_LOGI(TAG, "Rotary encoder initialized");
    return ESP_OK;
}

int rotary_get_count(void)
{
    return encoder_count;
}

bool rotary_is_pressed(void)
{
    return encoder_pressed;
}
