#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_smartconfig.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "ssd1306.h"
#include "font8x8_basic.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "driver/gpio.h"

// 引脚定义
#define SSD1306_SCLK 4
#define SSD1306_MOSI 5
#define SSD1306_DC   6
#define SSD1306_CS   1
#define SSD1306_RES  2

// 编码器引脚定义
#define ENCODER_KEY 16
#define ENCODER_S1  17
#define ENCODER_S2  42

// 字库文件路径
#define FONT_FILE_PATH "/spiffs/GT32L24M0140.bin"
#define UNI2GBK_FILE_PATH "/spiffs/uni2gbk.bin"
#define WIFI_CONFIG_FILE_PATH "/spiffs/wifi_config.json"
#define MQTT_CONFIG_FILE_PATH "/spiffs/mqtt_config.json"

// SmartConfig超时时间（秒）
#define SMARTCONFIG_TIMEOUT_SEC 60

// WiFi重连延迟（毫秒）
#define WIFI_RECONNECT_DELAY_MS 5000

// SSD1306设备结构体
SSD1306_t dev;

// 日志标签
static const char *TAG = "idfzk";

// 字库文件句柄
static FILE *font_file = NULL;
// uni2gbk映射文件句柄
static FILE *uni2gbk_file = NULL;

// WiFi状态
typedef enum {
    WIFI_STATUS_DISCONNECTED,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_SMARTCONFIG
} wifi_status_t;

static wifi_status_t wifi_status = WIFI_STATUS_DISCONNECTED;
static bool wifi_connected = false;
static bool smartconfig_done = false;

// MQTT状态
typedef enum {
    MQTT_STATUS_DISCONNECTED,
    MQTT_STATUS_CONNECTING,
    MQTT_STATUS_CONNECTED
} mqtt_status_t;

static mqtt_status_t mqtt_status = MQTT_STATUS_DISCONNECTED;
static esp_mqtt_client_handle_t mqtt_client = NULL;

// 编码器相关变量
static int screen_brightness = 255; // 屏幕亮度（0-255）
static bool screen_enabled = true; // 屏幕是否启用
static int last_s1 = 0; // 编码器S1引脚上次状态
static int last_s2 = 0; // 编码器S2引脚上次状态

// 函数声明
void init_ssd1306(void);
void update_display(void);
esp_err_t init_spiffs(void);
esp_err_t load_font_file(void);
void display_char_from_font(uint16_t gbk_code, int x, int y, int font_size);
void display_chinese_string(const char *utf8_str, int x, int y, int font_size, int spacing, int max_width);
void display_english_string(const char *text, int x, int y, int font_size);
uint32_t get_font_address(uint16_t gbk_code, int font_size);
void convert_horizontal_to_vertical_12x12(const uint8_t *src, uint8_t *dst);
void convert_horizontal_to_vertical_16x16(const uint8_t *src, uint8_t *dst);
void convert_horizontal_to_vertical_24x24(const uint8_t *src, uint8_t *dst);
uint16_t utf8_to_gbk(const char *utf8_str);
void test_uni2gbk_conversion(void);

// WiFi相关函数声明
esp_err_t wifi_init(void);
esp_err_t wifi_read_config(char *ssid, char *password);
esp_err_t wifi_save_config(const char *ssid, const char *password);
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void smartconfig_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
void wifi_connect(const char *ssid, const char *password);
void wifi_start_smartconfig(void);
void wifi_display_status(void);

// MQTT相关函数声明
esp_err_t mqtt_read_config(char *server, int *port, char *username, char *password, char *client_id, cJSON **sub_topics);
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
esp_err_t mqtt_init(void);

// 编码器相关函数声明
void encoder_init(void);
void encoder_task(void *pvParameters);
void encoder_handle_rotation(int s1, int s2);
void encoder_handle_key(void);
esp_err_t mqtt_connect(void);
esp_err_t mqtt_subscribe_topics(cJSON *sub_topics);
void mqtt_display_status(void);
void mqtt_display_message(const char *topic, const char *message);

/* [字库]：[HZK1616宋体] [数据排列]:从左到右从上到下 [取模方式]:纵向8点下高位 [正负反色]:否 [去掉重复后]共4个字符
 [总字符库]："胖达科技"*/

/*-- ID:0,字符:"胖",ASCII编码:C5D6,对应字:宽x高=16x16,画布:宽W=16 高H=16,共32字节--*/
static const uint8_t font_pang[32] = {
    0x00,0xfe,0x22,0x22,0xfe,0x44,0x48,0x50,0x40,0xff,0x40,0x50,0x48,0x44,0x00,0x00,
    0x80,0x7f,0x02,0x82,0xff,0x04,0x04,0x04,0x04,0xff,0x04,0x04,0x04,0x06,0x04,0x00
};

/*-- ID:1,字符:"达",ASCII编码:B4EF,对应字:宽x高=16x16,画布:宽W=16 高H=16,共32字节--*/
static const uint8_t font_da[32] = {
    0x40,0x42,0xcc,0x00,0x20,0x20,0x20,0xa0,0x7f,0x20,0x20,0x20,0x30,0x20,0x00,0x00,
    0x40,0x20,0x1f,0x20,0x50,0x48,0x46,0x41,0x40,0x41,0x42,0x4c,0x58,0x60,0x20,0x00
};

/*-- ID:2,字符:"科",ASCII编码:BFC6,对应字:宽x高=16x16,画布:宽W=16 高H=16,共32字节--*/
static const uint8_t font_ke[32] = {
    0x24,0x24,0x24,0xa4,0xfe,0xa3,0x22,0x00,0x24,0x48,0x00,0xff,0x00,0x80,0x00,0x00,
    0x10,0x08,0x06,0x01,0xff,0x00,0x01,0x02,0x02,0x02,0x02,0xff,0x01,0x01,0x01,0x00
};

/*-- ID:3,字符:"技",ASCII编码:BCBC,对应字:宽x高=16x16,画布:宽W=16 高H=16,共32字节--*/
static const uint8_t font_ji[32] = {
    0x10,0x10,0x10,0xff,0x10,0x10,0x88,0x88,0x88,0xff,0x88,0x88,0x8c,0x08,0x00,0x00,
    0x04,0x44,0x82,0x7f,0x01,0x80,0x81,0x46,0x28,0x10,0x28,0x26,0x41,0xc0,0x40,0x00
};

/* [字库]：[HZK1212宋体] [数据排列]:从左到右从上到下 [取模方式]:纵向8点下高位 [正负反色]:否 [去掉重复后]共4个字符
 [总字符库]："胖达科技"*/

/*-- ID:0,字符:"胖",ASCII编码:C5D6,对应字:宽x高=12x12,画布:宽W=12 高H=16,共24字节--*/
static const uint8_t font_pang_12[24] = {
    0x00,0xf8,0x28,0x28,0xf8,0x40,0x70,0x40,0xfc,0x60,0x58,0x40,
    0x00,0x0f,0x01,0x01,0x0f,0x02,0x02,0x02,0x0f,0x02,0x02,0x02
};

/*-- ID:1,字符:"达",ASCII编码:B4EF,对应字:宽x高=12x12,画布:宽W=12 高H=16,共24字节--*/
static const uint8_t font_da_12[24] = {
    0x80,0x88,0xc8,0x90,0x20,0x20,0x20,0xfc,0x20,0x20,0x30,0x20,
    0x00,0x00,0x0f,0x00,0x08,0x04,0x03,0x00,0x01,0x02,0x04,0x08
};

/*-- ID:2,字符:"科",ASCII编码:BFC6,对应字:宽x高=12x12,画布:宽W=12 高H=16,共24字节--*/
static const uint8_t font_ke_12[24] = {
    0x28,0x28,0xfc,0xa4,0x24,0x20,0x88,0x30,0x00,0xfc,0x00,0x00,
    0x06,0x01,0x0f,0x00,0x01,0x04,0x04,0x03,0x02,0x0f,0x01,0x01
};

/*-- ID:3,字符:"技",ASCII编码:BCBC,对应字:宽x高=12x12,画布:宽W=12 高H=16,共24字节--*/
static const uint8_t font_ji_12[24] = {
    0x20,0x20,0xfc,0xa0,0xa0,0x90,0x90,0xfc,0x90,0x90,0x98,0x10,
    0x01,0x01,0x0f,0x00,0x00,0x01,0x0a,0x04,0x0a,0x01,0x00,0x00
};

/* [字库]：[HZK2424仿宋] [数据排列]:从左到右从上到下 [取模方式]:纵向8点下高位 [正负反色]:否 [去掉重复后]共4个字符
 [总字符库]："胖达科技"*/

/*-- ID:0,字符:"胖",ASCII编码:C5D6,对应字:宽x高=24x24,画布:宽W=24 高H=24,共72字节--*/
static const uint8_t font_pang_24[72] = {
    0x00,0x00,0x00,0x00,0x04,0xf8,0x18,0x08,0xf8,0xfc,0x00,0x10,0xe0,0xc0,0x00,0xff,
    0x0e,0x00,0xc0,0x38,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xfc,0x7f,0x23,0x23,
    0xff,0xff,0x80,0x84,0xc4,0xc5,0x44,0xff,0x46,0x47,0x42,0x42,0x62,0x60,0x60,0x00,
    0x00,0x00,0xc0,0x38,0x0f,0x00,0x20,0x60,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0xff,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

/*-- ID:1,字符:"达",ASCII编码:B4EF,对应字:宽x高=24x24,画布:宽W=24 高H=24,共72字节--*/
static const uint8_t font_da_24[72] = {
    0x00,0x00,0x00,0x00,0x08,0x18,0x70,0x00,0x00,0x00,0x00,0x00,0x00,0xfb,0xfe,0x00,
    0x00,0x00,0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x08,0x18,0x08,0x08,0x08,0xfc,0x1c,
    0x04,0x02,0x02,0x02,0xe2,0x3f,0x13,0x23,0x61,0xc1,0x81,0x01,0x01,0x00,0x00,0x00,
    0x00,0x10,0x10,0x10,0x10,0x10,0x1f,0x13,0x10,0x38,0x26,0x23,0x60,0x60,0x40,0x40,
    0xc0,0xc1,0xc3,0xc3,0xc0,0x40,0x40,0x00
};

/*-- ID:2,字符:"科",ASCII编码:BFC6,对应字:宽x高=24x24,画布:宽W=24 高H=24,共72字节--*/
static const uint8_t font_ke_24[72] = {
    0x00,0x00,0x00,0x20,0x10,0x10,0x18,0xf8,0x0c,0x04,0x00,0x00,0x18,0x30,0x60,0x00,
    0x33,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02,0x82,0x62,0x3a,0x0f,0xff,
    0x09,0x19,0xb1,0x80,0x86,0x8c,0x98,0xc0,0xfe,0xff,0x40,0x60,0x60,0x20,0x00,0x00,
    0x00,0x04,0x03,0x01,0x00,0x00,0xe4,0xff,0x00,0x00,0x01,0x01,0x00,0x00,0x00,0x00,
    0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00
};

/*-- ID:3,字符:"技",ASCII编码:BCBC,对应字:宽x高=24x24,画布:宽W=24 高H=24,共72字节--*/
static const uint8_t font_ji_24[72] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xfe,0x00,0x80,0x00,0x00,0x80,0x80,0x81,0xfe,
    0x80,0x80,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x80,0x82,0xc3,0x61,0xff,0xff,
    0x11,0x09,0x01,0x51,0xd1,0x91,0x1f,0x0f,0xc8,0x78,0x1c,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x01,0x00,0x20,0xc0,0xff,0x3f,0x00,0x80,0xc0,0x60,0x20,0x19,0x0e,0x0f,
    0x39,0x60,0xe0,0xc0,0x80,0x80,0x80,0x00
};

// 初始化SSD1306
void init_ssd1306(void)
{
    ESP_LOGI(TAG, "Initializing SSD1306...");
    
    // 初始化SPI接口
    spi_master_init(&dev, SSD1306_MOSI, SSD1306_SCLK, SSD1306_CS, SSD1306_DC, SSD1306_RES);
    
    // 初始化SSD1306
    ssd1306_init(&dev, 128, 64);
    
    // 清屏
    ssd1306_clear_screen(&dev, false);
    ssd1306_show_buffer(&dev);
    
    ESP_LOGI(TAG, "SSD1306 initialized successfully");
}

// 初始化编码器
void encoder_init(void)
{
    ESP_LOGI(TAG, "Initializing encoder...");
    
    // 配置编码器引脚
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << ENCODER_KEY) | (1ULL << ENCODER_S1) | (1ULL << ENCODER_S2);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);
    
    // 读取初始状态
    last_s1 = gpio_get_level(ENCODER_S1);
    last_s2 = gpio_get_level(ENCODER_S2);
    
    ESP_LOGI(TAG, "Encoder initialized successfully");
}

// 编码器任务
void encoder_task(void *pvParameters)
{
    while (1) {
        // 读取编码器状态
        int s1 = gpio_get_level(ENCODER_S1);
        int s2 = gpio_get_level(ENCODER_S2);
        int key = gpio_get_level(ENCODER_KEY);
        
        // 处理旋转
        if (s1 != last_s1 || s2 != last_s2) {
            encoder_handle_rotation(s1, s2);
            last_s1 = s1;
            last_s2 = s2;
        }
        
        // 处理按键
        static bool last_key = 1;
        if (key != last_key) {
            if (key == 0) { // 按键按下
                encoder_handle_key();
            }
            last_key = key;
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// 处理编码器旋转
void encoder_handle_rotation(int s1, int s2)
{
    if (screen_enabled) {
        // 顺时针旋转：增加亮度
        if (s1 != s2) {
            screen_brightness += 10;
            if (screen_brightness > 255) {
                screen_brightness = 255;
            }
        } else { // 逆时针旋转：减少亮度
            screen_brightness -= 10;
            if (screen_brightness < 0) {
                screen_brightness = 0;
            }
        }
        
        // 设置屏幕亮度
        ssd1306_set_contrast(&dev, screen_brightness);
        ESP_LOGI(TAG, "Screen brightness set to: %d", screen_brightness);
    }
}

// 处理编码器按键
void encoder_handle_key(void)
{
    screen_enabled = !screen_enabled;
    
    if (screen_enabled) {
        // 开启屏幕
        ssd1306_on(&dev);
        ssd1306_set_contrast(&dev, screen_brightness);
        // 刷新显示
        ssd1306_show_buffer(&dev);
        ESP_LOGI(TAG, "Screen enabled");
    } else {
        // 关闭屏幕
        ssd1306_off(&dev);
        ESP_LOGI(TAG, "Screen disabled");
    }
}

// 更新显示 - 同时显示三种字体大小的"胖达科技"
void update_display(void)
{
    ESP_LOGI(TAG, "Updating display...");
    
    // 清屏
    ssd1306_clear_screen(&dev, false);
    ssd1306_show_buffer(&dev);
    
    ESP_LOGI(TAG, "Drawing '胖达科技' in three font sizes...");
    
    // ========== 12x12字体（顶部，第0-1页） ==========
    // 每个汉字12像素宽，4个汉字共48像素
    // 起始x坐标 = (128 - 48) / 2 = 40
    int x_12 = 40;
    int page_12 = 0; // y=0
    
    // 显示"胖"（12x12）
    ssd1306_display_image(&dev, page_12, x_12, font_pang_12, 12);
    ssd1306_display_image(&dev, page_12 + 1, x_12, font_pang_12 + 12, 12);
    
    // 显示"达"（12x12）
    ssd1306_display_image(&dev, page_12, x_12 + 12, font_da_12, 12);
    ssd1306_display_image(&dev, page_12 + 1, x_12 + 12, font_da_12 + 12, 12);
    
    // 显示"科"（12x12）
    ssd1306_display_image(&dev, page_12, x_12 + 24, font_ke_12, 12);
    ssd1306_display_image(&dev, page_12 + 1, x_12 + 24, font_ke_12 + 12, 12);
    
    // 显示"技"（12x12）
    ssd1306_display_image(&dev, page_12, x_12 + 36, font_ji_12, 12);
    ssd1306_display_image(&dev, page_12 + 1, x_12 + 36, font_ji_12 + 12, 12);
    
    // ========== 16x16字体（中间，第2-3页） ==========
    // 每个汉字16像素宽，4个汉字共64像素
    // 起始x坐标 = (128 - 64) / 2 = 32
    int x_16 = 32;
    int page_16 = 2; // y=16
    
    // 显示"胖"（16x16）
    ssd1306_display_image(&dev, page_16, x_16, font_pang, 16);
    ssd1306_display_image(&dev, page_16 + 1, x_16, font_pang + 16, 16);
    
    // 显示"达"（16x16）
    ssd1306_display_image(&dev, page_16, x_16 + 16, font_da, 16);
    ssd1306_display_image(&dev, page_16 + 1, x_16 + 16, font_da + 16, 16);
    
    // 显示"科"（16x16）
    ssd1306_display_image(&dev, page_16, x_16 + 32, font_ke, 16);
    ssd1306_display_image(&dev, page_16 + 1, x_16 + 32, font_ke + 16, 16);
    
    // 显示"技"（16x16）
    ssd1306_display_image(&dev, page_16, x_16 + 48, font_ji, 16);
    ssd1306_display_image(&dev, page_16 + 1, x_16 + 48, font_ji + 16, 16);
    
    // ========== 24x24字体（底部，第5-7页） ==========
    // 每个汉字24像素宽，4个汉字共96像素，超出屏幕宽度
    // 改为从x=16开始显示，或者只显示部分
    // 这里从x=16开始，总宽度96像素
    int x_24 = 16;
    int page_24 = 5; // y=40
    
    // 显示"胖"（24x24）- 分为3页，每页24字节
    ssd1306_display_image(&dev, page_24, x_24, font_pang_24, 24);
    ssd1306_display_image(&dev, page_24 + 1, x_24, font_pang_24 + 24, 24);
    ssd1306_display_image(&dev, page_24 + 2, x_24, font_pang_24 + 48, 24);
    
    // 显示"达"（24x24）
    ssd1306_display_image(&dev, page_24, x_24 + 24, font_da_24, 24);
    ssd1306_display_image(&dev, page_24 + 1, x_24 + 24, font_da_24 + 24, 24);
    ssd1306_display_image(&dev, page_24 + 2, x_24 + 24, font_da_24 + 48, 24);
    
    // 显示"科"（24x24）
    ssd1306_display_image(&dev, page_24, x_24 + 48, font_ke_24, 24);
    ssd1306_display_image(&dev, page_24 + 1, x_24 + 48, font_ke_24 + 24, 24);
    ssd1306_display_image(&dev, page_24 + 2, x_24 + 48, font_ke_24 + 48, 24);
    
    // 显示"技"（24x24）
    ssd1306_display_image(&dev, page_24, x_24 + 72, font_ji_24, 24);
    ssd1306_display_image(&dev, page_24 + 1, x_24 + 72, font_ji_24 + 24, 24);
    ssd1306_display_image(&dev, page_24 + 2, x_24 + 72, font_ji_24 + 48, 24);
    
    ESP_LOGI(TAG, "Display updated successfully");
}

// 初始化SPIFFS文件系统
esp_err_t init_spiffs(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS...");
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS initialized successfully");
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }
    
    return ret;
}

// 加载字库文件
esp_err_t load_font_file(void)
{
    ESP_LOGI(TAG, "Loading font file: %s", FONT_FILE_PATH);
    
    // 检查文件是否存在
    struct stat st;
    if (stat(FONT_FILE_PATH, &st) != 0) {
        ESP_LOGE(TAG, "Font file not found: %s", FONT_FILE_PATH);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Font file size: %ld bytes", st.st_size);
    
    // 打开字库文件
    font_file = fopen(FONT_FILE_PATH, "rb");
    if (font_file == NULL) {
        ESP_LOGE(TAG, "Failed to open font file");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Font file loaded successfully");
    
    // 加载uni2gbk映射文件
    ESP_LOGI(TAG, "Loading uni2gbk mapping file: %s", UNI2GBK_FILE_PATH);
    
    // 检查文件是否存在
    if (stat(UNI2GBK_FILE_PATH, &st) != 0) {
        ESP_LOGE(TAG, "Uni2gbk file not found: %s", UNI2GBK_FILE_PATH);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Uni2gbk file size: %ld bytes", st.st_size);
    
    // 打开uni2gbk文件
    uni2gbk_file = fopen(UNI2GBK_FILE_PATH, "rb");
    if (uni2gbk_file == NULL) {
        ESP_LOGE(TAG, "Failed to open uni2gbk file");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Uni2gbk file loaded successfully");
    
    return ESP_OK;
}

// 横向取模转纵向取模（12x12）
void convert_horizontal_to_vertical_12x12(const uint8_t *src, uint8_t *dst)
{
    // 初始化目标数组
    memset(dst, 0, 24);
    
    // 横向取模转纵向取模
    // 横向数据：12行 × 2字节/行 = 24字节
    // 纵向数据：2页 × 12字节/页 = 24字节
    for (int row = 0; row < 12; row++) {
        int page = row / 8;
        int row_in_page = row % 8;
        for (int byte_idx = 0; byte_idx < 2; byte_idx++) {
            uint8_t byte_val = src[row * 2 + byte_idx];
            for (int bit = 0; bit < 8; bit++) {
                int col = byte_idx * 8 + bit;
                if (col >= 12) continue;
                int pixel = (byte_val >> (7 - bit)) & 0x01;
                if (pixel) {
                    dst[page * 12 + col] |= (1 << row_in_page);
                }
            }
        }
    }
    
    // 打印转换前后的数据（调试用）
    // ESP_LOGI(TAG, "Original vs Converted data (12x12):");
    // for (int i = 0; i < 24; i++) {
    //     printf("%02X ", src[i]);
    //     if ((i + 1) % 8 == 0) {
    //         printf(" -> ");
    //         for (int j = i - 7; j <= i; j++) {
    //             printf("%02X ", dst[j]);
    //         }
    //         printf("\n");
    //     }
    // }
}

// 横向取模转纵向取模（24x24）
void convert_horizontal_to_vertical_24x24(const uint8_t *src, uint8_t *dst)
{
    // 初始化目标数组
    memset(dst, 0, 72);
    
    // 横向取模转纵向取模
    // 横向数据：24行 × 3字节/行 = 72字节
    // 纵向数据：3页 × 24字节/页 = 72字节
    for (int row = 0; row < 24; row++) {
        int page = row / 8;
        int row_in_page = row % 8;
        for (int byte_idx = 0; byte_idx < 3; byte_idx++) {
            uint8_t byte_val = src[row * 3 + byte_idx];
            for (int bit = 0; bit < 8; bit++) {
                int col = byte_idx * 8 + bit;
                int pixel = (byte_val >> (7 - bit)) & 0x01;
                if (pixel) {
                    dst[page * 24 + col] |= (1 << row_in_page);
                }
            }
        }
    }
    
    // 打印转换前后的数据（调试用）
    // ESP_LOGI(TAG, "Original vs Converted data (24x24):");
    // for (int i = 0; i < 72; i++) {
    //     printf("%02X ", src[i]);
    //     if ((i + 1) % 8 == 0) {
    //         printf(" -> ");
    //         for (int j = i - 7; j <= i; j++) {
    //             printf("%02X ", dst[j]);
    //         }
    //         printf("\n");
    //     }
    // }
}

// 横向取模转纵向取模（16x16）
void convert_horizontal_to_vertical_16x16(const uint8_t *src, uint8_t *dst)
{
    // 初始化目标数组
    memset(dst, 0, 32);
    
    // 横向取模转纵向取模
    // 横向数据：16行 × 2字节/行 = 32字节
    // 纵向数据：2页 × 16字节/页 = 32字节
    for (int row = 0; row < 16; row++) {
        int page = row / 8;
        int row_in_page = row % 8;
        for (int byte_idx = 0; byte_idx < 2; byte_idx++) {
            uint8_t byte_val = src[row * 2 + byte_idx];
            for (int bit = 0; bit < 8; bit++) {
                int col = byte_idx * 8 + bit;
                int pixel = (byte_val >> (7 - bit)) & 0x01;
                if (pixel) {
                    dst[page * 16 + col] |= (1 << row_in_page);
                }
            }
        }
    }
    
    // 打印转换前后的数据（调试用）
    // ESP_LOGI(TAG, "Original vs Converted data:");
    // for (int i = 0; i < 32; i++) {
    //     printf("%02X ", src[i]);
    //     if ((i + 1) % 8 == 0) {
    //         printf(" -> ");
    //         for (int j = i - 7; j <= i; j++) {
    //             printf("%02X ", dst[j]);
    //         }
    //         printf("\n");
    //     }
    // }
    
    // 打印硬编码数据（对比用）
    // ESP_LOGI(TAG, "Hardcoded font_pang data:");
    // for (int i = 0; i < 32; i++) {
    //     printf("%02X ", font_pang[i]);
    //     if ((i + 1) % 8 == 0) printf("\n");
    // }
}

// 计算汉字点阵在芯片中的地址（参考system.md）
unsigned long gt(unsigned char c1, unsigned char c2, unsigned char c3, unsigned char c4)
{
    unsigned long h = 0;
    if (c2 == 0x7f) return h;
    
    if (c1 >= 0xA1 && c1 <= 0xAB && c2 >= 0xa1) { // Section 1
        h = (c1 - 0xA1) * 94 + (c2 - 0xA1);
    } else if (c1 >= 0xa8 && c1 <= 0xa9 && c2 < 0xa1) { // Section 5
        if (c2 > 0x7f) c2--;
        h = (c1 - 0xa8) * 96 + (c2 - 0x40) + 846;
    }
    
    if (c1 >= 0xb0 && c1 <= 0xf7 && c2 >= 0xa1) { // Section 2
        h = (c1 - 0xB0) * 94 + (c2 - 0xA1) + 1038;
    } else if (c1 < 0xa1 && c1 >= 0x81 && c2 >= 0x40) { // Section 3
        if (c2 > 0x7f) c2--;
        h = (c1 - 0x81) * 190 + (c2 - 0x40) + 1038 + 6768;
    } else if (c1 >= 0xaa && c2 < 0xa1) { // Section 4
        if (c2 > 0x7f) c2--;
        h = (c1 - 0xaa) * 96 + (c2 - 0x40) + 1038 + 12848;
    } else if (c1 == 0x81 && c2 >= 0x39) { // 四字节区1
        h = 1038 + 21008 + (c3 - 0xEE) * 10 + c4 - 0x39;
    } else if (c1 == 0x82) { // 四字节区2
        h = 1038 + 21008 + 161 + (c2 - 0x30) * 1260 + (c3 - 0x81) * 10 + c4 - 0x30;
    }
    
    return h;
}

// 获取字库中字符的地址
uint32_t get_font_address(uint16_t gbk_code, int font_size)
{
    uint32_t base_address = 0;
    uint32_t char_size = 0;
    uint8_t c1 = (gbk_code >> 8) & 0xFF;
    uint8_t c2 = gbk_code & 0xFF;
    
    if (font_size == 12) {
        // 12x12点阵，起始地址0x093D0E，每个字符24字节
        base_address = 0x093D0E;
        char_size = 24;
    } else if (font_size == 16) {
        // 16x16点阵，起始地址0x114FDE，每个字符32字节
        base_address = 0x114FDE;
        char_size = 32;
    } else if (font_size == 24) {
        // 24x24点阵，起始地址0x1F43DE，每个字符72字节
        base_address = 0x1F43DE;
        char_size = 72;
    }
    
    // 使用gt函数计算偏移量
    unsigned long h = gt(c1, c2, 0x00, 0x00);
    uint32_t offset = h * char_size;
    
    // ESP_LOGI(TAG, "gt(c1=0x%02X, c2=0x%02X) = %lu, offset=0x%08" PRIX32 ", base=0x%08" PRIX32 ", total=0x%08" PRIX32,
    //          c1, c2, h, offset, base_address, base_address + offset);
    
    return base_address + offset;
}

// 从字库中提取字符数据并显示
void display_char_from_font(uint16_t gbk_code, int x, int y, int font_size)
{
    if (font_file == NULL) {
        ESP_LOGE(TAG, "Font file not loaded");
        return;
    }
    
    uint32_t address = get_font_address(gbk_code, font_size);
    uint8_t char_size = 0;
    int pages = 0;
    
    if (font_size == 12) {
        char_size = 24;
        pages = 2;
    } else if (font_size == 16) {
        char_size = 32;
        pages = 2;
    } else if (font_size == 24) {
        char_size = 72;
        pages = 3;
    }
    
    // ESP_LOGI(TAG, "Reading character at address: 0x%08" PRIX32 ", size: %d bytes", address, char_size);
    
    // 定位到字符地址
    fseek(font_file, address, SEEK_SET);
    
    // 读取字符数据
    uint8_t *char_data = (uint8_t *)malloc(char_size);
    if (char_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for character data");
        return;
    }
    
    size_t bytes_read = fread(char_data, 1, char_size, font_file);
    if (bytes_read != char_size) {
        ESP_LOGE(TAG, "Failed to read character data: read %d bytes, expected %d bytes", bytes_read, char_size);
        free(char_data);
        return;
    }
    
    // 转换数据格式（横向取模转纵向取模）
    if (font_size == 12) {
        uint8_t *converted_data = (uint8_t *)malloc(char_size);
        if (converted_data != NULL) {
            convert_horizontal_to_vertical_12x12(char_data, converted_data);
            free(char_data);
            char_data = converted_data;
            
            // 打印转换后的数据（调试用）
            // ESP_LOGI(TAG, "Converted character data (12x12, first 16 bytes):");
            // for (int i = 0; i < 16 && i < char_size; i++) {
            //     printf("%02X ", char_data[i]);
            //     if ((i + 1) % 8 == 0) printf("\n");
            // }
        }
    } else if (font_size == 16) {
        uint8_t *converted_data = (uint8_t *)malloc(char_size);
        if (converted_data != NULL) {
            convert_horizontal_to_vertical_16x16(char_data, converted_data);
            free(char_data);
            char_data = converted_data;
            
            // 打印转换后的数据（调试用）
            // ESP_LOGI(TAG, "Converted character data (first 16 bytes):");
            // for (int i = 0; i < 16 && i < char_size; i++) {
            //     printf("%02X ", char_data[i]);
            //     if ((i + 1) % 8 == 0) printf("\n");
            // }
        }
    } else if (font_size == 24) {
        uint8_t *converted_data = (uint8_t *)malloc(char_size);
        if (converted_data != NULL) {
            convert_horizontal_to_vertical_24x24(char_data, converted_data);
            free(char_data);
            char_data = converted_data;
            
            // 打印转换后的数据（调试用）
            // ESP_LOGI(TAG, "Converted character data (24x24, first 16 bytes):");
            // for (int i = 0; i < 16 && i < char_size; i++) {
            //     printf("%02X ", char_data[i]);
            //     if ((i + 1) % 8 == 0) printf("\n");
            // }
        }
    }
    
    // 打印字符数据（调试用）
    // ESP_LOGI(TAG, "Character data (first 16 bytes):");
    // for (int i = 0; i < 16 && i < char_size; i++) {
    //     printf("%02X ", char_data[i]);
    //     if ((i + 1) % 8 == 0) printf("\n");
    // }
    // if (char_size > 16) {
    //     printf("... (total %d bytes)\n", char_size);
    // }
    
    // 显示字符
    int page = y / 8;
    int bytes_per_page = font_size;
    
    for (int i = 0; i < pages; i++) {
        ssd1306_display_image(&dev, page + i, x, char_data + i * bytes_per_page, bytes_per_page);
    }
    
    free(char_data);
}

// UTF-8转GBK编码
uint16_t utf8_to_gbk(const char *utf8_str)
{
    uint32_t unicode = 0;
    
    if ((utf8_str[0] & 0x80) == 0x00) {
        // ASCII字符
        unicode = utf8_str[0];
    } else if ((utf8_str[0] & 0xE0) == 0xC0) {
        // 2字节UTF-8
        unicode = ((utf8_str[0] & 0x1F) << 6) | (utf8_str[1] & 0x3F);
    } else if ((utf8_str[0] & 0xF0) == 0xE0) {
        // 3字节UTF-8（中文字符）
        unicode = ((utf8_str[0] & 0x0F) << 12) | ((utf8_str[1] & 0x3F) << 6) | (utf8_str[2] & 0x3F);
    } else if ((utf8_str[0] & 0xF8) == 0xF0) {
        // 4字节UTF-8
        unicode = ((utf8_str[0] & 0x07) << 18) | ((utf8_str[1] & 0x3F) << 12) | 
                 ((utf8_str[2] & 0x3F) << 6) | (utf8_str[3] & 0x3F);
    }
    
    // 从uni2gbk.bin文件中查找GBK编码
    if (uni2gbk_file != NULL && unicode < 65536) {
        // 计算文件偏移量（每个Unicode码点对应2个字节）
        uint32_t offset = unicode * 2;
        
        // 定位到对应位置
        if (fseek(uni2gbk_file, offset, SEEK_SET) == 0) {
            uint8_t gbk_bytes[2];
            size_t read_size = fread(gbk_bytes, 1, 2, uni2gbk_file);
            
            if (read_size == 2) {
                // 读取成功，返回GBK编码（大端格式）
                uint16_t gbk_code = (gbk_bytes[0] << 8) | gbk_bytes[1];
                if (gbk_code != 0) {
                    return gbk_code;
                }
            }
        }
    }
    
    // 如果文件未加载或读取失败，返回0
    return 0;
}

// 测试uni2gbk转换功能，比较硬编码GBK码与从文件中获取的GBK码
void test_uni2gbk_conversion(void)
{
    ESP_LOGI(TAG, "\n========== Testing uni2gbk conversion ==========");
    
    // 定义测试用例：汉字及其硬编码GBK码
    struct test_case {
        const char *chinese;  // 汉字（UTF-8）
        uint16_t hardcoded_gbk;  // 硬编码的GBK码
    } test_cases[] = {
        {"江", 0xBDAD},
        {"西", 0xCEF7},
        {"省", 0xCAA1},
        {"萍", 0xC6BC},
        {"乡", 0xCFE7},
        {"市", 0xCAD0},
        {"胖", 0xC5D6},
        {"达", 0xB4EF},
        {"科", 0xBFC6},
        {"技", 0xBCBC},
        {"路", 0xC2B7},
        {"明", 0xC3F7},
        {"电", 0xB5E7},
        {"脑", 0xC4D4},
        {"中", 0xD6D0},
        {"国", 0xB9FA},
        {"人", 0xC8CB},
        {"民", 0xC3F1},
        // 新增测试文本：这是第一个测试
        {"这", 0xD5E2},
        {"是", 0xCAC7},
        {"第", 0xB5DA},
        {"一", 0xD2BB},
        {"个", 0xB8F6},
        {"测", 0xB2E2},
        {"试", 0xCAF5},
        // 新增测试文本：我爱李美春
        {"我", 0xCED2},
        {"爱", 0xB0AE},
        {"李", 0xC0EE},
        {"美", 0xC3C0},
        {"春", 0xB4BA},
        // 新增测试文本：西班牙王国
        {"西", 0xCEF7},
        {"班", 0xB0E0},
        {"牙", 0xD1C0},
        {"王", 0xCDF5},
        {"国", 0xB9FA}
    };
    
    int test_count = sizeof(test_cases) / sizeof(test_cases[0]);
    int pass_count = 0;
    
    for (int i = 0; i < test_count; i++) {
        const char *chinese = test_cases[i].chinese;
        uint16_t hardcoded_gbk = test_cases[i].hardcoded_gbk;
        
        // 从uni2gbk.bin文件中获取GBK码
        uint16_t file_gbk = utf8_to_gbk(chinese);
        
        // 比较结果
        bool passed = (file_gbk == hardcoded_gbk);
        if (passed) {
            pass_count++;
        }
        
        // 打印测试结果
        ESP_LOGI(TAG, "%s: hardcoded=0x%04X, file=0x%04X, %s", 
                 chinese, hardcoded_gbk, file_gbk, 
                 passed ? "PASS" : "FAIL");
    }
    
    // 打印总体测试结果
    ESP_LOGI(TAG, "\nTest Summary: %d/%d passed", pass_count, test_count);
    if (pass_count == test_count) {
        ESP_LOGI(TAG, "All tests passed! The uni2gbk.bin conversion is working correctly.");
    } else {
        ESP_LOGE(TAG, "Some tests failed. Please check the uni2gbk.bin file or conversion logic.");
    }
}

// 显示中文字符串（支持混显）
void display_chinese_string(const char *utf8_str, int x, int y, int font_size, int spacing, int max_width)
{
    int current_x = x;
    int current_y = y;
    const char *p = utf8_str;
    
    while (*p) {
        if ((*p & 0x80) == 0x00) {
            // ASCII字符（英文、数字、符号）
            if (current_x + 8 > max_width) { // 8是ASCII字符宽度
                current_x = x;
                // 根据字体大小调整行高
                if (font_size == 12) {
                    current_y += 16; // 12px字体实际占用16像素高度（2页）
                } else {
                    current_y += font_size;
                }
                if (current_y + font_size > 64) { // 64是屏幕高度
                    break; // 超出屏幕范围，停止显示
                }
            }
            
            // 显示ASCII字符（中线对齐）
            int line_height = font_size;
            if (font_size == 12) {
                line_height = 16; // 12px字体实际占用16像素高度
            }
            // 计算行中线位置，确保ASCII字符在当前行垂直居中
            int ascii_y = current_y + (line_height - 8) / 2; // 8是ASCII字符高度
            int page = ascii_y / 8;
            uint8_t image[8];
            memcpy(image, font8x8_basic_tr[(uint8_t)*p], 8);
            ssd1306_display_image(&dev, page, current_x, image, 8);
            current_x += 8; // ASCII字符宽度（8像素）
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            // 2字节UTF-8
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
            // 3字节UTF-8（中文字符）
            if (current_x + spacing > max_width) {
                current_x = x;
                // 根据字体大小调整行高
                if (font_size == 12) {
                    current_y += 16; // 12px字体实际占用16像素高度（2页）
                } else {
                    current_y += font_size;
                }
                if (current_y + font_size > 64) {
                    break;
                }
            }
            
            uint16_t gbk_code = 0;
            
            // 使用utf8_to_gbk函数从uni2gbk.bin文件中获取GBK码
            gbk_code = utf8_to_gbk(p);
            
            if (gbk_code != 0) {
                display_char_from_font(gbk_code, current_x, current_y, font_size);
                current_x += spacing;
            }
            
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
            // 4字节UTF-8
            p += 4;
        }
    }
}

// 显示英文字符串
void display_english_string(const char *text, int x, int y, int font_size)
{
    int page = y / 8;
    int offset = y % 8;
    
    // 根据字体大小选择合适的显示函数
    if (font_size == 12) {
        // 使用标准文本显示（8x8字体）
        ssd1306_display_text(&dev, page, text, strlen(text), false);
    } else if (font_size == 16) {
        // 使用标准文本显示（8x8字体，两行）
        ssd1306_display_text(&dev, page, text, strlen(text), false);
        if (offset > 0) {
            ssd1306_display_text(&dev, page + 1, text, strlen(text), false);
        }
    } else if (font_size == 24) {
        // 使用标准文本显示（8x8字体，三行）
        ssd1306_display_text(&dev, page, text, strlen(text), false);
        ssd1306_display_text(&dev, page + 1, text, strlen(text), false);
        ssd1306_display_text(&dev, page + 2, text, strlen(text), false);
    }
}

// ==================== WiFi功能实现 ====================

// 读取WiFi配置文件
esp_err_t wifi_read_config(char *ssid, char *password)
{
    FILE *file = fopen(WIFI_CONFIG_FILE_PATH, "r");
    if (file == NULL) {
        ESP_LOGW(TAG, "WiFi config file not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    // 读取文件内容
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *buffer = (char *)malloc(file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        ESP_LOGE(TAG, "Failed to allocate memory for config file");
        return ESP_ERR_NO_MEM;
    }
    
    size_t read_size = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (read_size != file_size) {
        free(buffer);
        ESP_LOGE(TAG, "Failed to read config file");
        return ESP_FAIL;
    }
    buffer[file_size] = '\0';
    
    // 解析JSON
    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse WiFi config JSON");
        return ESP_FAIL;
    }
    
    cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    cJSON *password_item = cJSON_GetObjectItem(root, "password");
    
    if (ssid_item == NULL || password_item == NULL) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Invalid WiFi config format");
        return ESP_FAIL;
    }
    
    strcpy(ssid, ssid_item->valuestring);
    strcpy(password, password_item->valuestring);
    
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "WiFi config loaded: SSID=%s", ssid);
    return ESP_OK;
}

// 保存WiFi配置文件
esp_err_t wifi_save_config(const char *ssid, const char *password)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_FAIL;
    }
    
    cJSON_AddStringToObject(root, "ssid", ssid);
    cJSON_AddStringToObject(root, "password", password);
    
    char *json_string = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON");
        return ESP_FAIL;
    }
    
    FILE *file = fopen(WIFI_CONFIG_FILE_PATH, "w");
    if (file == NULL) {
        free(json_string);
        ESP_LOGE(TAG, "Failed to open WiFi config file for writing");
        return ESP_FAIL;
    }
    
    fprintf(file, "%s", json_string);
    fclose(file);
    free(json_string);
    
    ESP_LOGI(TAG, "WiFi config saved: SSID=%s", ssid);
    return ESP_OK;
}

// WiFi事件处理函数
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi station started");
                wifi_status = WIFI_STATUS_DISCONNECTED;
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "WiFi disconnected");
                wifi_connected = false;
                wifi_status = WIFI_STATUS_DISCONNECTED;
                
                // 自动重连 - 只在SmartConfig完成后才自动重连
                if (smartconfig_done) {
                    ESP_LOGI(TAG, "Attempting to reconnect...");
                    esp_wifi_connect();
                }
                break;
                
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP:
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
                wifi_connected = true;
                wifi_status = WIFI_STATUS_CONNECTED;
                break;
                
            default:
                break;
        }
    }
}

// SmartConfig事件处理函数
void smartconfig_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == SC_EVENT) {
        switch (event_id) {
            case SC_EVENT_SCAN_DONE:
                ESP_LOGI(TAG, "SmartConfig scan done");
                break;
                
            case SC_EVENT_FOUND_CHANNEL:
                ESP_LOGI(TAG, "SmartConfig found channel");
                break;
                
            case SC_EVENT_GOT_SSID_PSWD:
                ESP_LOGI(TAG, "SmartConfig got SSID and password");
                
                smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
                
                // 保存WiFi配置
                char ssid[33] = {0};
                char password[65] = {0};
                memcpy(ssid, evt->ssid, sizeof(evt->ssid));
                memcpy(password, evt->password, sizeof(evt->password));
                
                ESP_LOGI(TAG, "SSID: %s", ssid);
                
                // 在OLED屏幕上显示SSID信息
                ssd1306_clear_screen(&dev, false);
                display_chinese_string("SmartConfig成功", 0, 0, 12, 14, 128);
                display_chinese_string("SSID:", 0, 16, 12, 14, 128);
                
                // 显示SSID（英文）
                char ssid_display[33] = {0};
                strcpy(ssid_display, ssid);
                display_english_string(ssid_display, 32, 16, 12);
                
                ssd1306_show_buffer(&dev);
                
                // 保存配置到文件
                wifi_save_config(ssid, password);
                
                // 连接WiFi
                wifi_connect(ssid, password);
                break;
                
            case SC_EVENT_SEND_ACK_DONE:
                ESP_LOGI(TAG, "SmartConfig send ack done");
                smartconfig_done = true;
                esp_smartconfig_stop();
                break;
                
            default:
                break;
        }
    }
}

// 连接WiFi
void wifi_connect(const char *ssid, const char *password)
{
    wifi_config_t wifi_config = {0};
    strcpy((char *)wifi_config.sta.ssid, ssid);
    strcpy((char *)wifi_config.sta.password, password);
    
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", ssid);
    
    wifi_status = WIFI_STATUS_CONNECTING;
    
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
}

// 启动SmartConfig
void wifi_start_smartconfig(void)
{
    ESP_LOGI(TAG, "Starting SmartConfig...");
    
    wifi_status = WIFI_STATUS_SMARTCONFIG;
    smartconfig_done = false;
    
    // 注册SmartConfig事件处理
    esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &smartconfig_event_handler, NULL);
    
    // 启动SmartConfig
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    // 在ESP-IDF v5.3中，SmartConfig类型通过esp_smartconfig_set_type设置
    esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS);
    esp_smartconfig_start(&cfg);
    
    // 等待SmartConfig完成
    while (!smartconfig_done) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "SmartConfig waiting for config...");
    }
    
    // 等待WiFi连接成功
    while (!wifi_connected) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "SmartConfig waiting for WiFi connection...");
    }
    
    ESP_LOGI(TAG, "SmartConfig completed successfully");
}

// 初始化WiFi
esp_err_t wifi_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi...");
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 初始化TCP/IP协议栈
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // 注册WiFi事件处理
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
    // 设置WiFi模式为Station
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi initialized successfully");
    return ESP_OK;
}

// 在OLED上显示WiFi状态
void wifi_display_status(void)
{
    ssd1306_clear_screen(&dev, false);
    
    switch (wifi_status) {
        case WIFI_STATUS_DISCONNECTED:
            display_chinese_string("WiFi未连接", 0, 0, 16, 18, 128);
            display_chinese_string("请使用SmartConfig", 0, 16, 16, 18, 128);
            break;
            
        case WIFI_STATUS_CONNECTING:
            display_chinese_string("WiFi连接中...", 0, 0, 16, 18, 128);
            break;
            
        case WIFI_STATUS_CONNECTED:
            display_chinese_string("WiFi已连接", 0, 0, 16, 18, 128);
            // 可以显示IP地址等信息
            break;
            
        case WIFI_STATUS_SMARTCONFIG:
            display_chinese_string("SmartConfig模式", 0, 0, 16, 18, 128);
            display_chinese_string("请配置WiFi", 0, 16, 16, 18, 128);
            break;
    }
    
    ssd1306_show_buffer(&dev);
}

// ==================== MQTT功能实现 ====================

// 读取MQTT配置文件
esp_err_t mqtt_read_config(char *server, int *port, char *username, char *password, char *client_id, cJSON **sub_topics)
{
    FILE *file = fopen(MQTT_CONFIG_FILE_PATH, "r");
    if (file == NULL) {
        ESP_LOGW(TAG, "MQTT config file not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    // 读取文件内容
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    char *buffer = (char *)malloc(file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        ESP_LOGE(TAG, "Failed to allocate memory for MQTT config file");
        return ESP_ERR_NO_MEM;
    }
    
    size_t read_size = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (read_size != file_size) {
        free(buffer);
        ESP_LOGE(TAG, "Failed to read MQTT config file");
        return ESP_FAIL;
    }
    buffer[file_size] = '\0';
    
    // 解析JSON
    cJSON *root = cJSON_Parse(buffer);
    free(buffer);
    
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse MQTT config JSON");
        return ESP_FAIL;
    }
    
    cJSON *server_item = cJSON_GetObjectItem(root, "server");
    cJSON *port_item = cJSON_GetObjectItem(root, "port");
    cJSON *username_item = cJSON_GetObjectItem(root, "username");
    cJSON *password_item = cJSON_GetObjectItem(root, "password");
    cJSON *client_id_item = cJSON_GetObjectItem(root, "client_id");
    cJSON *sub_topics_item = cJSON_GetObjectItem(root, "sub_topics");
    
    if (server_item == NULL || port_item == NULL || username_item == NULL || password_item == NULL || client_id_item == NULL) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Invalid MQTT config format");
        return ESP_FAIL;
    }
    
    strcpy(server, server_item->valuestring);
    *port = port_item->valueint;
    strcpy(username, username_item->valuestring);
    strcpy(password, password_item->valuestring);
    strcpy(client_id, client_id_item->valuestring);
    
    if (sub_topics_item != NULL) {
        *sub_topics = cJSON_Duplicate(sub_topics_item, true);
    }
    
    cJSON_Delete(root);
    
    ESP_LOGI(TAG, "MQTT config loaded: server=%s, port=%d, username=%s", server, *port, username);
    return ESP_OK;
}

// MQTT事件处理函数
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "MQTT event id: %" PRIi32 "", event_id);
    
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    // esp_mqtt_client_handle_t client = event->client; // 未使用的变量
    
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            mqtt_status = MQTT_STATUS_CONNECTED;
            mqtt_display_status();
            
            // 读取配置文件并订阅主题
            char server[128] = {0};
            int port = 1883;
            char username[64] = {0};
            char password[64] = {0};
            char client_id[64] = {0};
            cJSON *sub_topics = NULL;
            
            if (mqtt_read_config(server, &port, username, password, client_id, &sub_topics) == ESP_OK) {
                if (sub_topics != NULL) {
                    mqtt_subscribe_topics(sub_topics);
                    cJSON_Delete(sub_topics);
                }
            }
            break;
            
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            mqtt_status = MQTT_STATUS_DISCONNECTED;
            mqtt_display_status();
            break;
            
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT subscribed to topic: %.*s", event->topic_len, event->topic);
            break;
            
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT message received: topic=%.*s, data=%.*s", 
                     event->topic_len, event->topic, event->data_len, event->data);
            
            // 显示收到的消息
            char topic[128] = {0};
            char message[256] = {0};
            strncpy(topic, event->topic, event->topic_len);
            strncpy(message, event->data, event->data_len);
            mqtt_display_message(topic, message);
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error: type=%d", event->error_handle->error_type);
            mqtt_status = MQTT_STATUS_DISCONNECTED;
            mqtt_display_status();
            break;
            
        default:
            break;
    }
}

// 初始化MQTT
esp_err_t mqtt_init(void)
{
    ESP_LOGI(TAG, "Initializing MQTT...");
    
    // 读取MQTT配置
    char server[128] = {0};
    int port = 1883;
    char username[64] = {0};
    char password[64] = {0};
    char client_id[64] = {0};
    cJSON *sub_topics = NULL;
    
    esp_err_t ret = mqtt_read_config(server, &port, username, password, client_id, &sub_topics);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MQTT config");
        return ret;
    }
    
    // 构建MQTT配置
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = server,
        .credentials.username = username,
        .credentials.authentication.password = password,
        .credentials.client_id = client_id,
        .session.disable_clean_session = false,
        .session.keepalive = 60,
        .network.timeout_ms = 30000, // 增加MQTT连接超时时间到30秒
        .buffer.size = 1024,
        .buffer.out_size = 1024,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    // 创建MQTT客户端
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        if (sub_topics != NULL) {
            cJSON_Delete(sub_topics);
        }
        return ESP_FAIL;
    }
    
    // 注册MQTT事件处理
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    
    if (sub_topics != NULL) {
        cJSON_Delete(sub_topics);
    }
    
    ESP_LOGI(TAG, "MQTT initialized successfully");
    return ESP_OK;
}

// 连接MQTT服务器
esp_err_t mqtt_connect(void)
{
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Connecting to MQTT server...");
    mqtt_status = MQTT_STATUS_CONNECTING;
    mqtt_display_status();
    
    esp_err_t ret = esp_mqtt_client_start(mqtt_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        mqtt_status = MQTT_STATUS_DISCONNECTED;
        mqtt_display_status();
        return ret;
    }
    
    return ESP_OK;
}

// 订阅主题
esp_err_t mqtt_subscribe_topics(cJSON *sub_topics)
{
    if (mqtt_client == NULL || sub_topics == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized or no topics to subscribe");
        return ESP_FAIL;
    }
    
    int array_size = cJSON_GetArraySize(sub_topics);
    for (int i = 0; i < array_size; i++) {
        cJSON *topic_item = cJSON_GetArrayItem(sub_topics, i);
        if (cJSON_IsString(topic_item)) {
            const char *topic = topic_item->valuestring;
            int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic, 0);
            ESP_LOGI(TAG, "Subscribing to topic: %s, msg_id: %d", topic, msg_id);
        }
    }
    
    return ESP_OK;
}

// 在OLED上显示MQTT状态
void mqtt_display_status(void)
{
    ssd1306_clear_screen(&dev, false);
    
    switch (mqtt_status) {
        case MQTT_STATUS_DISCONNECTED:
            display_chinese_string("MQTT未连接", 0, 0, 16, 18, 128);
            break;
            
        case MQTT_STATUS_CONNECTING:
            display_chinese_string("MQTT连接中...", 0, 0, 16, 18, 128);
            break;
            
        case MQTT_STATUS_CONNECTED:
            display_chinese_string("MQTT已连接", 0, 0, 16, 18, 128);
            break;
    }
    
    ssd1306_show_buffer(&dev);
}

// 保存上次的主题
static char last_topic[256] = "";

// 在OLED上显示MQTT消息
void mqtt_display_message(const char *topic, const char *message)
{
    // 检查主题是否变化
    if (strcmp(topic, last_topic) != 0) {
        // 主题变化，全屏更新
        ssd1306_clear_screen(&dev, false);
        
        // 显示主题
        display_chinese_string("主题:", 0, 0, 12, 14, 128);
        
        // 显示主题内容（支持自动换行）
        int topic_x = 32;
        int topic_y = 0;
        const char *topic_p = topic;
        while (*topic_p) {
            if (topic_x + 8 > 128) { // 8是ASCII字符宽度
                topic_x = 0;
                topic_y += 16; // 12px字体使用16像素行高
                if (topic_y + 16 > 32) { // 32是消息内容起始位置
                    break;
                }
            }
            int line_height = 16; // 12px字体实际占用16像素高度
            int ascii_y = topic_y + (line_height - 8) / 2; // 8是ASCII字符高度
            int page = ascii_y / 8;
            uint8_t image[8];
            memcpy(image, font8x8_basic_tr[(uint8_t)*topic_p], 8);
            ssd1306_display_image(&dev, page, topic_x, image, 8);
            topic_x += 8; // ASCII字符宽度（8像素）
            topic_p++;
        }
        
        // 显示消息标签
        display_chinese_string("消息:", 0, 16, 12, 14, 128);
        
        // 保存当前主题
        strncpy(last_topic, topic, sizeof(last_topic) - 1);
        last_topic[sizeof(last_topic) - 1] = '\0';
    } else {
        // 主题未变化，只清除消息区域
        // 清除消息内容区域（从y=32开始）
        for (int page = 4; page < 8; page++) { // 32/8=4, 64/8=8
            for (int seg = 0; seg < 128; seg++) {
                dev._page[page]._segs[seg] = 0;
            }
        }
    }
    
    // 显示消息内容（支持混显和自动换行）
    display_chinese_string(message, 0, 32, 12, 14, 128);
    
    ssd1306_show_buffer(&dev);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing ESP32-S3 OLED display module");
    
    // 初始化SPIFFS文件系统
    if (init_spiffs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS");
        return;
    }
    
    // 加载字库文件
    if (load_font_file() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load font file");
        return;
    }
    
    // 测试字库文件的前几个字节（调试用）
    // ESP_LOGI(TAG, "Testing font file first 16 bytes:");
    // uint8_t test_buffer[16];
    // fseek(font_file, 0, SEEK_SET);
    // size_t test_read = fread(test_buffer, 1, 16, font_file);
    // if (test_read == 16) {
    //     for (int i = 0; i < 16; i++) {
    //         printf("%02X ", test_buffer[i]);
    //         if ((i + 1) % 8 == 0) printf("\n");
    //     }
    // } else {
    //     ESP_LOGE(TAG, "Failed to read test data: read %d bytes", test_read);
    // }
    
    // 测试uni2gbk转换功能
    // test_uni2gbk_conversion();
    
    // 初始化SSD1306
    init_ssd1306();
    
    // 清屏
    ssd1306_clear_screen(&dev, false);
    ssd1306_show_buffer(&dev);
    
    // 初始化编码器
    encoder_init();
    
    // 创建编码器任务
    xTaskCreate(encoder_task, "encoder_task", 2048, NULL, 5, NULL);
    
    // ==================== WiFi功能初始化 ====================
    ESP_LOGI(TAG, "========== WiFi Initialization ==========");
    
    // 初始化WiFi
    if (wifi_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi");
        return;
    }
    
    // 显示WiFi状态
    wifi_display_status();
    
    // 尝试读取WiFi配置文件
    char ssid[33] = {0};
    char password[65] = {0};
    esp_err_t config_ret = wifi_read_config(ssid, password);
    
    if (config_ret == ESP_OK) {
        // 配置文件存在，尝试连接WiFi
        ESP_LOGI(TAG, "Found WiFi config, attempting to connect...");
        wifi_connect(ssid, password);
        
        // 等待连接结果，最多尝试5次
        int connect_attempts = 0;
        const int max_attempts = 5;
        
        while (!wifi_connected && connect_attempts < max_attempts) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            connect_attempts++;
            wifi_display_status();
            ESP_LOGI(TAG, "Waiting for WiFi connection... attempt %d/%d", connect_attempts, max_attempts);
        }
        
        if (wifi_connected) {
            ESP_LOGI(TAG, "WiFi connected successfully!");
            wifi_display_status();
        } else {
            ESP_LOGW(TAG, "WiFi connection failed after %d attempts, starting SmartConfig...", max_attempts);
            // 连接失败，启动SmartConfig
            wifi_start_smartconfig();
        }
    } else {
        // 配置文件不存在，启动SmartConfig
        ESP_LOGI(TAG, "No WiFi config found, starting SmartConfig...");
        wifi_start_smartconfig();
    }
    
    // 显示最终WiFi状态
    wifi_display_status();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    ESP_LOGI(TAG, "========== Testing font library ==========");
    
    // 测试1：使用16x16字体显示"胖达科技"
    ESP_LOGI(TAG, "Test 1: Displaying '胖达科技' with 16x16 font");
    display_chinese_string("胖达科技", 0, 0, 16, 18, 128);
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    // 测试2：使用12x12字体显示"路明电脑"
    ESP_LOGI(TAG, "Test 2: Displaying '路明电脑' with 12x12 font");
    ssd1306_clear_screen(&dev, false);
    ssd1306_show_buffer(&dev);
    display_chinese_string("路明电脑", 0, 0, 12, 14, 128);
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    // 测试3：使用24x24字体显示"路明电脑"
    ESP_LOGI(TAG, "Test 3: Displaying '路明电脑' with 24x24 font");
    ssd1306_clear_screen(&dev, false);
    ssd1306_show_buffer(&dev);
    display_chinese_string("路明电脑", 0, 0, 24, 28, 128);
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    // 测试4：使用12x12字体显示"江西省萍乡市"
    ESP_LOGI(TAG, "Test 4: Displaying '江西省萍乡市' with 12x12 font");
    ssd1306_clear_screen(&dev, false);
    ssd1306_show_buffer(&dev);
    display_chinese_string("江西省萍乡市", 0, 0, 12, 14, 128);
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    // 测试5：使用16x16字体显示"江西省萍乡市"
    ESP_LOGI(TAG, "Test 5: Displaying '江西省萍乡市' with 16x16 font");
    ssd1306_clear_screen(&dev, false);
    ssd1306_show_buffer(&dev);
    display_chinese_string("江西省萍乡市", 0, 0, 16, 18, 128);
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    // 测试6：使用24x24字体显示"江西省萍乡市"
    ESP_LOGI(TAG, "Test 6: Displaying '江西省萍乡市' with 24x24 font");
    ssd1306_clear_screen(&dev, false);
    ssd1306_show_buffer(&dev);
    display_chinese_string("江西省萍乡市", 0, 0, 24, 28, 128);
    
    vTaskDelay(5000 / portTICK_PERIOD_MS);
    
    ESP_LOGI(TAG, "========== Font library test completed ==========");
    
    // ==================== MQTT功能初始化 ====================
    ESP_LOGI(TAG, "========== MQTT Initialization ==========");
    
    // 初始化MQTT
    if (mqtt_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MQTT");
    } else {
        // 连接MQTT服务器
        if (mqtt_connect() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to connect to MQTT server");
        } else {
            ESP_LOGI(TAG, "MQTT initialization completed");
        }
    }
    
    // 主循环
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}