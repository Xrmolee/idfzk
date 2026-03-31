# 从字库读取数据并显示到SSD1306的技术说明

## 概述
本文档说明如何从GT32L24M0140字库文件中读取汉字点阵数据，并将其正确显示到SSD1306 OLED屏幕上。

## 1. 设置SPIFFS分区和挂载字库文件

### 1.1 分区配置
在 `partitions.csv` 文件中配置SPIFFS分区：

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000, 0x6000,
phy_init, data, phy,     0xf000, 0x1000,
factory,   data, nvs,     0x1f000,0x1000,
storage,   data, spiffs,  0x30000,0x100000,
assets,   data, spiffs,  0x130000,0x400000,
```

### 1.2 CMakeLists.txt配置
在 `main/CMakeLists.txt` 中指定SPIFFS分区镜像：

```cmake
spiffs_create_partition_image(assets ../assets FLASH_IN_PROJECT)
```

### 1.3 SPIFFS初始化和挂载

```c
esp_err_t init_spiffs(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    
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
        } else if (ret == ESP_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ESP_FAIL;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS total: %d, used: %d", total, used);
    }
    
    return ESP_OK;
}
```

### 1.4 加载字库文件

```c
static FILE *font_file = NULL;

esp_err_t load_font_file(void)
{
    font_file = fopen(FONT_FILE_PATH, "rb");
    if (font_file == NULL) {
        ESP_LOGE(TAG, "Failed to open font file: %s", FONT_FILE_PATH);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Font file loaded successfully");
    return ESP_OK;
}
```

## 2. 从字库获得地址

### 2.1 字库地址信息

| 字体大小 | 起始地址 | 单字占用字节 | 每行字节数 |
|---------|-----------|-------------|-------------|
| 12x12   | 0x093D0E | 24字节      | 2字节       |
| 16x16   | 0x114FDE | 32字节      | 2字节       |
| 24x24   | 0x1F43DE | 72字节      | 3字节       |

### 2.2 GBK编码计算函数

```c
unsigned long gt(unsigned char c1, unsigned char c2, unsigned char c3, unsigned char c4)
{
    unsigned long h = 0;
    if (c2 == 0x7f) return (h);
    
    // Section 1: 0xA1-0xA9, 0xA1-0xFE（标点符号区）
    if (c1 >= 0xA1 && c1 <= 0xA9 && c2 >= 0xA1) {
        h = (c1 - 0xA1) * 94 + (c2 - 0xA1);
    }
    // Section 5: 0xA8-0xA9, 0x40-0xA0
    else if (c1 >= 0xA8 && c1 <= 0xA9 && c2 < 0xA1) {
        if (c2 > 0x7F) c2--;
        h = (c1 - 0xA8) * 96 + (c2 - 0x40) + 846;
    }
    // Section 2: 0xB0-0xF7, 0xA1-0xFE（常用汉字区）
    else if (c1 >= 0xB0 && c1 <= 0xF7 && c2 >= 0xA1) {
        h = (c1 - 0xB0) * 94 + (c2 - 0xA1) + 1038;
    }
    // Section 3: 0x81-0xA0, 0x40-0xFE
    else if (c1 < 0xA1 && c1 >= 0x81 && c2 >= 0x40) {
        if (c2 > 0x7F) c2--;
        h = (c1 - 0x81) * 190 + (c2 - 0x40) + 1038 + 6768;
    }
    // Section 4: 0xAA-0xFE, 0x40-0xA0
    else if (c1 >= 0xAA && c2 < 0xA1) {
        if (c2 > 0x7F) c2--;
        h = (c1 - 0xAA) * 96 + (c2 - 0x40) + 1038 + 12848;
    }
    // 四字节区（仅16x16和24x24字体支持）
    else if (c1 == 0x81 && c2 >= 0x39) {
        h = 1038 + 21008 + (c3 - 0xEE) * 10 + c4 - 0x39;
    }
    else if (c1 == 0x82) {
        h = 1038 + 21008 + 161 + (c2 - 0x30) * 1260 + (c3 - 0x81) * 10 + c4 - 0x30;
    }
    
    return (h);
}
```

### 2.3 地址计算函数

```c
uint32_t get_font_address(uint16_t gbk_code, int font_size)
{
    uint32_t base_address = 0;
    uint32_t char_size = 0;
    uint8_t c1 = (gbk_code >> 8) & 0xFF;
    uint8_t c2 = gbk_code & 0xFF;
    
    if (font_size == 12) {
        base_address = 0x093D0E;
        char_size = 24;
    } else if (font_size == 16) {
        base_address = 0x114FDE;
        char_size = 32;
    } else if (font_size == 24) {
        base_address = 0x1F43DE;
        char_size = 72;
    }
    
    unsigned long h = gt(c1, c2, 0x00, 0x00);
    uint32_t offset = h * char_size;
    
    return base_address + offset;
}
```

### 2.4 调用示例

```c
// 获取"江"字（GBK编码：0xBDAD）的16x16字体地址
uint32_t address = get_font_address(0xBDAD, 16);
// 返回地址：0x114FDE + (索引值 * 32)
```

## 3. 转换成SSD1306正常显示的数据

### 3.1 数据格式说明

- **字库格式**：横向取模，数据按行存储
- **SSD1306格式**：纵向取模，数据按列存储
- **转换需求**：需要将横向取模转换为纵向取模

### 3.2 12x12字体转换函数

```c
void convert_horizontal_to_vertical_12x12(const uint8_t *src, uint8_t *dst)
{
    memset(dst, 0, 24);
    
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
}
```

### 3.3 16x16字体转换函数

```c
void convert_horizontal_to_vertical_16x16(const uint8_t *src, uint8_t *dst)
{
    memset(dst, 0, 32);
    
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
}
```

### 3.4 24x24字体转换函数

```c
void convert_horizontal_to_vertical_24x24(const uint8_t *src, uint8_t *dst)
{
    memset(dst, 0, 72);
    
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
}
```

### 3.5 显示字符函数

```c
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
    
    // 读取字符数据
    fseek(font_file, address, SEEK_SET);
    uint8_t *char_data = (uint8_t *)malloc(char_size);
    if (char_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return;
    }
    
    size_t bytes_read = fread(char_data, 1, char_size, font_file);
    if (bytes_read != char_size) {
        ESP_LOGE(TAG, "Failed to read character data");
        free(char_data);
        return;
    }
    
    // 转换数据格式
    uint8_t *converted_data = (uint8_t *)malloc(char_size);
    if (converted_data != NULL) {
        if (font_size == 12) {
            convert_horizontal_to_vertical_12x12(char_data, converted_data);
        } else if (font_size == 16) {
            convert_horizontal_to_vertical_16x16(char_data, converted_data);
        } else if (font_size == 24) {
            convert_horizontal_to_vertical_24x24(char_data, converted_data);
        }
        free(char_data);
        char_data = converted_data;
    }
    
    // 显示字符
    int page = y / 8;
    int bytes_per_page = font_size;
    
    for (int i = 0; i < pages; i++) {
        ssd1306_display_image(&dev, page + i, x, char_data + i * bytes_per_page, bytes_per_page);
    }
    
    free(char_data);
}
```

## 4. 注意事项

### 4.1 内存管理
- **内存分配**：使用 `malloc` 分配内存后必须使用 `free` 释放
- **内存泄漏**：确保所有分配的内存都有对应的释放操作
- **缓冲区大小**：根据字体大小分配正确的缓冲区大小（24/32/72字节）

### 4.2 文件操作
- **文件句柄**：确保字库文件已成功打开（`font_file != NULL`）
- **文件定位**：使用 `fseek` 定位到正确的地址
- **错误处理**：检查文件读取的字节数是否正确

### 4.3 数据转换
- **格式差异**：字库使用横向取模，SSD1306需要纵向取模
- **转换精度**：确保每个像素点都正确映射
- **边界检查**：12x12字体需要检查列是否超出范围（col >= 12）

### 4.4 显示参数
- **页面计算**：`page = y / 8`，确保显示在正确的页面
- **字节每页**：`bytes_per_page = font_size`，根据字体大小调整
- **字符间距**：建议间距为字体大小+2像素（12号字用14，16号字用18，24号字用28）

### 4.5 编码问题
- **UTF-8到GBK**：需要正确转换UTF-8编码的中文字符为GBK编码
- **GBK编码查询**：可以使用在线工具查询：https://www.23bei.com/tool/216.html
- **编码映射**：建议建立常用汉字的GBK编码映射表

### 4.6 性能优化
- **缓存机制**：对于频繁显示的字符，可以缓存转换后的数据
- **批量读取**：考虑一次性读取多个字符的数据，减少文件访问次数
- **内存优化**：在嵌入式系统中注意内存使用，避免频繁的内存分配和释放

## 5. 使用示例

### 5.1 显示单个字符

```c
// 显示"江"字（16号字体）
display_char_from_font(0xBDAD, 0, 0, 16);
```

### 5.2 显示字符串

```c
// 显示"江西省萍乡市"（12号字体）
display_chinese_string("江西省萍乡市", 0, 0, 12, 14);

// 显示"胖达科技"（16号字体）
display_chinese_string("胖达科技", 0, 0, 16, 18);

// 显示"路明电脑"（24号字体）
display_chinese_string("路明电脑", 0, 0, 24, 28);
```

### 5.3 完整初始化流程

```c
void app_main(void)
{
    // 1. 初始化SPIFFS
    if (init_spiffs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPIFFS");
        return;
    }
    
    // 2. 加载字库文件
    if (load_font_file() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load font file");
        return;
    }
    
    // 3. 初始化SSD1306
    init_ssd1306();
    
    // 4. 清屏
    ssd1306_clear_screen(&dev, false);
    ssd1306_show_buffer(&dev);
    
    // 5. 显示中文
    display_chinese_string("江西省萍乡市", 0, 0, 16, 18);
    
    // 6. 主循环
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
```

## 6. 故障排除

| 问题 | 可能原因 | 解决方案 |
|------|---------|---------|
| 中文显示乱码 | 数据格式转换错误 | 检查转换函数是否正确实现 |
| 字符显示不全 | 显示位置计算错误 | 调整 page 和 x 坐标 |
| 字库读取失败 | 字库文件未正确加载 | 检查 assets 目录和字库文件 |
| 内存不足 | 缓冲区大小不够 | 增加缓冲区大小或优化内存使用 |
| 字符重叠 | 字符间距不足 | 增加字符间距参数 |

## 7. 参考资源

- **汉字转编码工具**：https://www.23bei.com/tool/216.html
- **GT32L24M0140字库说明**：参考 `G:\aicomponents\fontDescription.txt`
- **SSD1306驱动**：参考项目中的 `ssd1306.h` 和 `ssd1306.c`

---

**版本**：v1.0  
**日期**：2026-03-29  
**适用平台**：ESP32-S3 + SSD1306 OLED 显示屏 + GT32L24M0140字库