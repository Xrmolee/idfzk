# ESP32-S3 汉字显示模块需求文档

## 1. 项目概述
在 ESP32-S3 平台上，基于 MQTT 协议接收 Unicode（UTF-8）格式的文本消息，通过高通字库芯片文件 GT32L24M0140.bin（GBK 编码）提取对应字符的点阵数据，最终在 SSD1306 OLED 显示屏上实现 12、16、24 三种可选字体大小的中文及 ASCII 字符显示。

## 2. 功能需求

### 2.1 字库使用
- 字库文件：GT32L24M0140.bin，存储于 ESP32-S3 的外部 Flash 分区中（作为只读数据分区）。
- 在 assets 分区中, 详细请查看 partitions.csv 文件。

### 2.2 编码转换
- 输入：MQTT 接收到的消息为 UTF-8 编码（Unicode）。
- 输出：将 UTF-8 字符串转换为 GBK 编码的字节序列，用于索引字库。
- 转换方法：可采用轻量级转换表（如常用汉字映射）或 ESP-IDF 提供的 iconv 库（若支持）。

### 2.3 字体大小
- 中文使用三种字体大小：12×12 像素、16×16 像素、24×24 像素。
- 英文、数字、符号使用三种对应字体：7x8、8x16、12x24。
- 可根据需求在运行时动态切换字体。

### 2.4 编码器功能
- 旋转编码器用于切换三种字体大小模式（12、16、24）。
- 切换时区分中英文：
  - 中文使用对应大小的字体（12×12、16×16、24×24）
  - 英文、数字、符号使用对应大小的字体（7x8、8x16、12x24）
- 旋转时实时刷新当前显示内容，以反映字体大小的变化。

### 2.5 显示输出
- 显示屏：SSD1306，128×64 分辨率，I²C 或 SPI 接口。
- 显示内容：将转换后的字符串按指定字体大小绘制到屏幕上，支持自动换行（超出屏幕宽度时换行）或固定位置输出。
- 显示区域：可设定起始坐标 (x, y)，支持清屏、滚动等基本功能。

### 2.6 WiFi连接设置
- **SmartConfig功能**：
  - 系统启动时检测WiFi连接状态
  - 如果WiFi未连接，自动启用SmartConfig功能
  - 用户通过手机APP（如ESP-TOUCH）发送WiFi配置信息
  - SmartConfig接收WiFi SSID和密码，自动连接到指定网络
  
- **WiFi配置保存**：
  - SmartConfig连接成功后，自动将WiFi信息保存到SPIFFS文件系统
  - 配置文件路径：`/spiffs/wifi_config.json`
  - 配置文件格式：JSON格式，包含SSID和密码
  - 示例格式：
    ```json
    {
      "ssid": "your_wifi_ssid",
      "password": "your_wifi_password"
    }
    ```
  
- **自动连接WiFi**：
  - 系统启动时，优先读取SPIFFS中的WiFi配置文件
  - 如果配置文件存在且有效，自动尝试连接到保存的WiFi网络
  - 如果连接失败或配置文件不存在，启用SmartConfig功能
  - 支持WiFi连接状态监控和自动重连
  
- **WiFi状态指示**：
  - 在OLED显示屏上显示WiFi连接状态
  - 连接中：显示"WiFi连接中..."
  - 连接成功：显示WiFi信号强度或IP地址
  - 连接失败：显示"WiFi连接失败，请使用SmartConfig"
  - SmartConfig模式：显示"请使用SmartConfig配置WiFi"

### 2.7 MQTT配置设置
- **MQTT配置文件**：
  - 配置文件路径：`/spiffs/mqtt_config.json`
  - 配置文件格式：JSON格式，包含MQTT服务器连接信息
  - 示例格式：
    ```json
    {
      "server": "mqtt_server_address",
      "port": 1883,
      "username": "mqtt_username",
      "password": "mqtt_password",
      "client_id": "esp32_device"
    }
    ```
  
- **配置项说明**：
  - `server`：MQTT服务器地址（支持mqtt://或mqtts://协议）
  - `port`：MQTT服务器端口（默认1883，TLS连接默认8883）
  - `username`：MQTT连接用户名
  - `password`：MQTT连接密码
  - `client_id`：MQTT客户端ID，建议使用唯一标识符
  - `sub_topics`：要订阅的主题列表，数组格式
  
- **配置文件加载**：
  - 系统启动时，从SPIFFS中读取MQTT配置文件
  - 如果配置文件存在且有效，使用配置的参数连接MQTT服务器
  - 如果配置文件不存在或无效，使用默认值或提示用户配置
  
- **MQTT状态指示**：
  - 在OLED显示屏上显示MQTT连接状态
  - 连接中：显示"MQTT连接中..."
  - 连接成功：显示"MQTT已连接"
  - 连接失败：显示"MQTT连接失败"
  - 订阅成功：显示"MQTT订阅成功"
  
- **订阅主题**：
  - 系统会自动订阅配置文件中指定的主题
  - 接收到订阅主题的消息时，会根据消息内容进行相应处理
  - 支持多个主题的订阅和处理

### 2.8 MQTT连接实现代码

以下是MQTT连接的核心实现代码：

```c
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

// MQTT事件处理函数
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "MQTT event id: %" PRIi32 "", event_id);
    
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    
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

// 在OLED上显示MQTT状态
void mqtt_display_status(void)
{
    ssd1306_clear_screen(&dev, false);
    
    switch (mqtt_status) {
        case MQTT_STATUS_DISCONNECTED:
            display_chinese_string("MQTT未连接", 0, 0, 16, 18);
            break;
            
        case MQTT_STATUS_CONNECTING:
            display_chinese_string("MQTT连接中...", 0, 0, 16, 18);
            break;
            
        case MQTT_STATUS_CONNECTED:
            display_chinese_string("MQTT已连接", 0, 0, 16, 18);
            break;
    }
    
    ssd1306_show_buffer(&dev);
}

// 在OLED上显示MQTT消息
void mqtt_display_message(const char *topic, const char *message)
{
    ssd1306_clear_screen(&dev, false);
    
    // 显示主题
    display_chinese_string("主题:", 0, 0, 12, 14);
    display_english_string(topic, 32, 0, 12);
    
    // 显示消息
    display_chinese_string("消息:", 0, 16, 12, 14);
    
    // 检查消息是否包含中文
    bool has_chinese = false;
    const char *p = message;
    while (*p) {
        if ((*p & 0x80) != 0) {
            has_chinese = true;
            break;
        }
        p++;
    }
    
    if (has_chinese) {
        // 显示中文字符串
        display_chinese_string(message, 0, 32, 12, 14);
    } else {
        // 显示英文字符串
        display_english_string(message, 0, 32, 12);
    }
    
    ssd1306_show_buffer(&dev);
}
```

## 3. 技术要求

### 3.1 硬件平台
- 主控：ESP32-S3
- 存储：外置 Flash，用于存放字库文件（文件大小约几 MB）
- 显示：SSD1306 OLED 模块（SPI 接口）
- 编码器（Rotary）：
  - KEY: 16
  - S1: 17
  - S2: 42
- WiFi模块：
  - 内置WiFi模块，支持802.11 b/g/n协议
  - 支持2.4GHz频段
  - 支持Station模式
  - 支持SmartConfig配置功能

### 3.2 软件环境
- 开发框架：ESP-IDF v5.0 及以上
- 编程语言：C/C++
- WiFi组件：
  - ESP-IDF WiFi驱动
  - ESP-IDF SmartConfig组件（esp_smartconfig）
  - ESP-IDF NVS（非易失性存储）或SPIFFS文件系统用于配置存储
  - JSON解析库（如cJSON）用于配置文件解析
- MQTT组件：
  - ESP-IDF MQTT客户端库（esp-mqtt）
  - 支持TLS加密连接（如需连接mqtts://协议）
  - 支持自动重连和遗嘱消息

### 3.3 字库文件访问
- 将 GT32L24M0140.bin 烧录到自定义分区（如类型 data，子类型 spiffs 或 fat），或使用 esp_partition API 直接读取。
- 字库读取函数需根据 GBK 编码计算偏移地址，从分区中读取指定数量的字节（点阵数据）。

### 3.4 性能要求
- 单个汉字显示时间：≤ 50ms（包括 Flash 读取、点阵解析及屏幕绘制）
- 支持至少 20 个字符（含汉字）的实时显示刷新。

## 4. 字库文件说明

### 4.1 GT32L24M0140 标准汉字字库芯片

#### 规格书 DATASHEET
- 字符集：GB18030
- 兼容 Unicode
- 条形码（2套）：EAN13条码、CODE128条码
- 字号：12x12、16x16、24x24点阵
- 排置方式：横置横排
- 总线接口：SPI串行总线
- 封装类型：SOP8-B

### 4.2 字符点阵字库地址表

| NO. | 字库内容 | 编码体系（字符集） | 字符数 | 起始地址 | 参考算法 |
|-----|----------|-------------------|--------|----------|----------|
| 1 | 5X7点阵 ASCII标准字符 | ASCII | 96 | 0x080000 | 4.2.1 |
| 2 | 7X8点阵ASCII标准字符 | ASCII | 96 | 0x080300 | 4.2.2 |
| 3 | 7X8点阵ASCII粗体字符 | ASCII | 96 | 0x080600 | 4.2.3 |
| 4 | 6x12点阵ASCII字符 | ASCII | 96 | 0x080900 | 4.2.4 |
| 5 | 8x16点阵ASCII标准字符 | ASCII | 128 | 0x080D80 | 4.2.5 |
| 6 | 8x16点ASCII粗体字符 | ASCII | 96 | 0x081580 | 4.2.6 |
| 7 | 12x24点阵ASCII字符 | ASCII | 96 | 0x081B80 | 4.2.7 |
| 8 | 12x24点阵ASCII打印机字符 | ASCII | 224 | 0x3FAE10 | 4.3.3 |
| 9 | 16x32点阵ASCII标准字符 | ASCII | 96 | 0x082D80 | 4.2.8 |
| 10 | 16x32点阵ASCII粗体字符 | ASCII | 96 | 0x084580 | 4.2.9 |
| 11 | 12点阵不等宽ASCII方头（Arial）字符 | ASCII | 96 | 0x085D80 | 4.2.10 |
| 12 | 16点阵不等宽ASCII方头（Arial）字符 | ASCII | 96 | 0x086740 | 4.2.11 |
| 13 | 24点阵不等宽ASCII方头（Arial）字符 | ASCII | 96 | 0x087400 | 4.2.12 |
| 14 | 32点阵不等宽ASCII方头（Arial）字符 | ASCII | 96 | 0x088FC0 | 4.2.13 |
| 15 | 12点阵不等宽ASCII白正（Times New Roman）字符 | ASCII | 96 | 0x08C080 | 4.2.14 |
| 16 | 16点阵不等宽ASCII白正（Times New Roman）字符 | ASCII | 96 | 0x08CA50 | 4.2.15 |
| 17 | 24点阵不等宽ASCII白正（Times New Roman）字符 | ASCII | 96 | 0x08D740 | 4.2.16 |
| 18 | 32点阵不等宽ASCII白正（Times New Roman）字符 | ASCII | 96 | 0x08F340 | 4.2.17 |
| 19 | 14x28数字符号字符 | 数字符号字符 | 15 | 0x092400 | 4.2.18 |
| 20 | 20x40数字符号字符 | 数字符号字符 | 12 | 0x092748 | 4.2.19 |
| 21 | 28点阵不等宽数字符号字符 | 数字符号字符 | 15 | 0x092CE8 | 4.2.20 |
| 22 | 40点阵不等宽数字符号字符 | 数字符号字符 | 12 | 0x093396 | 4.2.21 |
| 23 | 12X12点阵GB18030汉字 | GB18030 | 20902 | 0x093D0E | 4.1.1 |
| 24 | 16X16点阵GB18030汉字 | GB18030 | 27484 | 0x114FDE | 4.1.2 |
| 25 | 24X24点阵GB18030汉字 | GB18030 | 27484 | 0x1F43DE | 4.1.3 |
| 26 | 12X12点阵GB18030字符 | GB18030 | 1038 | 0x093D0E | 4.1.1 |
| 27 | 16X16点阵GB18030字符 | GB18030 | 1038 | 0x114FDE | 4.1.2 |
| 28 | 24X24点阵GB18030字符 | GB18030 | 1038 | 0x1F43DE | 4.1.3 |
| 29 | Unicode->GBK 转码表 | 20902+ 1038 | - | 0x3EA90E | 4.3.1/4.3.2 |
| 30 | 12x27条形码字符 | EAN13 | 60 | 0x3F8FD2 | 4.2.22 |
| 31 | 16x20条形码字符 | CODE128 | 107 | 0x3F9C7A | 4.2.23 |
| 32 | 12x12天线符号 | - | 5 | 0x3FAD32 | 4.2.24 |
| 33 | 12x12电池符号 | - | 4 | 0x3FADAA | 4.2.25 |
| 34 | 保留区 | - | - | 0x3FD810~0x3FFFFF | - |

### 4.3 汉字字符点阵数据的地址计算

#### 4.3.1 12x12点阵GB18030汉字&字符
- 12x12点阵字库起始地址：BaseAdd＝0x093D0E
- Address：对应字符点阵在芯片中的字节地址。
- 地址的计算由下面的函数实现（ANSI C 语言编写）：

```c
/***************************************************************************************************
函数：unsigned long gt(unsigned char c1, unsigned char c2, unsigned char c3, unsigned char c4)
功能：计算汉字点阵在芯片中的地址
参数：汉字内码通过参数c1,c2传入，c3=0;c4=0. 注：12x12为GBK字符集，无四字节区。
返回：汉字点阵的字节地址(byte address)。如果用户是按 word mode 读取点阵数据，则其地址(word address)为字节地址除以2，即：word address = byte address / 2 .
例如：BaseAdd: 说明汉字点阵数据在字库芯片中的起始地址，即BaseAdd＝0x093D0E，
“啊”字的内码为0xb0a1,则byte address = gt(0xb0,0xa1,0x00,0x00) *24+BaseAdd
                               word address = byte address / 2
****************************************************************************************************/
unsigned long gt (unsigned char c1, unsigned char c2, unsigned char c3, unsigned char c4)
{
    unsigned long h=0;
    if(c2==0x7f) return (h);                      
    if(c1>=0xA1 && c1 <= 0xa9 && c2>=0xa1)      //Section 1
        h= (c1 - 0xA1) * 94 + (c2 - 0xA1);
    else if(c1>=0xa8 && c1 <= 0xa9 && c2<0xa1)   //Section 5
    {
        if(c2>0x7f)  
            c2--;                                
        h=(c1-0xa8)*96 + (c2-0x40)+846;   
    }  
    if(c1>=0xb0 && c1 <= 0xf7 && c2>=0xa1)      //Section 2
        h= (c1 - 0xB0) * 94 + (c2 - 0xA1)+1038;
    else if(c1<0xa1 && c1>=0x81 && c2>=0x40 )   //Section 3
    {
        if(c2>0x7f)  
            c2--;   

        h=(c1-0x81)*190 + (c2-0x40) + 1038 +6768;
    }
    else if(c1>=0xaa && c2<0xa1)                //Section 4
    {
        if(c2>0x7f)  
            c2--;                                
        h=(c1-0xaa)*96 + (c2-0x40) + 1038 +12848;
    }
    return(h);
}
Address = gt(c1,c2,0x00,0x00) *24+BaseAdd;
```

#### 4.3.2 16x16点阵GB18030汉字&字符
- 16x16点阵字库起始地址：BaseAdd＝0x114FDE
- Address：对应字符点阵在芯片中的字节地址。
- 地址的计算由下面的函数实现（ANSI C 语言编写）：

```c
/***************************************************************************************************
函数：unsigned long gt(unsigned char c1, unsigned char c2, unsigned char c3, unsigned char c4)
功能：计算汉字点阵在芯片中的地址
参数：c1,c2,c3,c4：4字节汉字内码通过参数c1,c2,c3,c4传入，双字节内码通过参数c1,c2传入，c3=0,c4=0
返回：汉字点阵的字节地址(byte address)。如果用户是按 word mode 读取点阵数据，则其地址(word address)为字节地址除以2，即：word address = byte address / 2 .
例如：BaseAdd: 说明汉字点阵数据在字库芯片中的起始地址，即BaseAdd＝0x114FDE;
“啊”字的内码为0xb0a1,则byte address = gt(0xb0,0xa1,0x00,0x00) *32+BaseAdd
                               word address = byte address / 2
     “ ”字的内码为0x8139ee39,则byte address = gt(0x81,0x39,0xee,0x39) *32+ BaseAdd
                               word address = byte address / 2
****************************************************************************************************/
unsigned long gt (unsigned char c1, unsigned char c2, unsigned char c3, unsigned char c4)
{
    unsigned long h=0;
    if(c2==0x7f) return (h);                      
    if(c1>=0xA1 && c1 <= 0xA0 && c2>=0xa1)      //Section 1
        h= (c1 - 0xA1) * 94 + (c2 - 0xA1);
    else if(c1>=0xa8 && c1 <= 0xa9 && c2<0xa1)   //Section 5
    {
        if(c2>0x7f)  
            c2--;     
             h=(c1-0xa8)*96 + (c2-0x40)+846;   
    }  
    if(c1>=0xb0 && c1 <= 0xf7 && c2>=0xa1)      //Section 2
        h= (c1 - 0xB0) * 94 + (c2 - 0xA1)+1038;
    else if(c1<0xa1 && c1>=0x81 && c2>=0x40 )  //Section 3
    {
        if(c2>0x7f)  
            c2--;                    
        h=(c1-0x81)*190 + (c2-0x40) + 1038 +6768;
    }
    else if(c1>=0xaa && c2<0xa1)                //Section 4
    {
        if(c2>0x7f)  
            c2--;                                
        h=(c1-0xaa)*96 + (c2-0x40) + 1038 +12848;
    }
    else  if(c1==0x81 && c2>=0x39) //四字节区1
    {
          h =1038 + 21008+(c3-0xEE)*10+c4-0x39;
    }
    else if(c1==0x82)//四字节区2
    {
          h =1038 + 21008+161+(c2-0x30)*1260+(c3-0x81)*10+c4-0x30;
    }
    return(h);
}
Address = gt(c1,c2,c3,c4) *32+BaseAdd;
```

#### 4.3.3 24x24点阵GB18030汉字&字符
- 24x24点阵字库起始地址：BaseAdd＝0x1F43DE
- Address：对应字符点阵在芯片中的字节地址。
- 地址的计算由下面的函数实现（ANSI C 语言编写）：

```c
/***************************************************************************************************
函数：unsigned long gt(unsigned char c1, unsigned char c2, unsigned char c3, unsigned char c4)
功能：计算汉字点阵在芯片中的地址
参数：c1,c2,c3,c4：4字节汉字内码通过参数c1,c2,c3,c4传入，双字节内码通过参数c1,c2传入，c3=0,c4=0
返回：汉字点阵的字节地址(byte address)。如果用户是按 word mode 读取点阵数据，则其地址(word address)为字节地址除以2，即：word address = byte address / 2 .
例如：BaseAdd: 说明汉字点阵数据在字库芯片中的起始地址，即BaseAdd＝0x1F43DE;
“啊”字的内码为0xb0a1,则byte address = gt(0xb0,0xa1,0x00,0x00) *72+BaseAdd
                               word address = byte address / 2
     “”字的内码为0x8139ee39,则byte address = gt(0x81, 0x39,0xee,0x39) *72+ BaseAdd
                               word address = byte address / 2
****************************************************************************************************/
unsigned long gt (unsigned char c1, unsigned char c2, unsigned char c3, unsigned char c4)
{
    unsigned long h=0;
    if(c2==0x7f) return (h);                      
    if(c1>=0xA1 && c1 <= 0xAB && c2>=0xa1)      //Section 1
        h= (c1 - 0xA1) * 94 + (c2 - 0xA1);
    else if(c1>=0xa8 && c1 <= 0xa9 && c2<0xa1)   //Section 5
    {
        if(c2>0x7f)  
            c2--;                                
        h=(c1-0xa8)*96 + (c2-0x40)+846;   
    }  
    if(c1>=0xb0 && c1 <= 0xf7 && c2>=0xa1)      //Section 2
        h= (c1 - 0xB0) * 94 + (c2 - 0xA1)+1038;
    else if(c1<0xa1 && c1>=0x81 && c2>=0x40)    //Section 3
    {
        if(c2>0x7f)  
            c2--;                    
        h=(c1-0x81)*190 + (c2-0x40) + 1038 +6768;
    }
    else if(c1>=0xaa && c2<0xa1)                //Section 4
    {
        if(c2>0x7f)  
            c2--;                                
        h=(c1-0xaa)*96 + (c2-0x40) + 1038 +12848;
    }
    else  if(c1==0x81 && c2>=0x39) //四字节区1
    {
          h =1038 + 21008+(c3-0xEE)*10+c4-0x39;
    }
    else if(c1==0x82)//四字节区2
    {
          h =1038 + 21008+161+(c2-0x30)*1260+(c3-0x81)*10+c4-0x30;
    }
    return(h);
}
Address = gt(c1,c2,c3,c4) *72+BaseAdd;
```

### 4.4 其它字符点阵数据的地址计算

#### 4.4.1 5x7 点阵ASCII 标准字符
- 参数说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x080000
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 8+BaseAdd

#### 4.4.2 7x8 点阵ASCII标准字符  
- 参数说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x080300
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 8+BaseAdd

#### 4.4.3 7x8 点阵ASCII粗体字符  
- 参数说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x080600
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 8+BaseAdd

#### 4.4.4 6x12点阵 ASCII 字符
- 参数说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x080900
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 12+BaseAdd  

#### 4.4.5 8x16点阵 ASCII标准字符  
- 参数说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x080D80
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 16+BaseAdd  

#### 4.4.6 8x16 点阵ASCII粗体字符  
- 参数说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x081580
  - if((ASCIICode>=0x20)&&(ASCIICode<=0x7F))
    - Address = (ASCIICode-0x20)*16+BaseAdd

#### 4.4.7 12x24 点阵 ASCII标准字符  
- 参数说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x081B80
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 48+BaseAdd

#### 4.4.8 16x32 点阵ASCII标准字符  
- 参数说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x082D80
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 64+BaseAdd

#### 4.4.9 16x32 点阵ASCII 粗体字符
- 参数说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x084580
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 64+BaseAdd

#### 4.4.10 12点阵不等宽ASCII方头（Arial）字符
- 说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x085D80
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 26 + BaseAdd

#### 4.4.11 16点阵不等宽ASCII方头（Arial）字符
- 说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x086740
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 34 + BaseAdd

#### 4.4.12 24点阵不等宽ASCII方头（Arial）字符
- 说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x087400
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 74 + BaseAdd

#### 4.4.13 32点阵不等宽ASCII方头（Arial）字符
- 说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x088FC0
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 130 + BaseAdd

#### 4.4.14 12点阵不等宽ASCII白正（Times New Roman）字符
- 说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x08C080
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) *26 + BaseAdd

#### 4.4.15 16点阵不等宽ASCII白正（Times New Roman）字符
- 参数说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x08CA50
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 34 + BaseAdd

#### 4.4.16 24点阵不等宽ASCII白正（Times New Roman）字符
- 说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x08D740
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 74+ BaseAdd

#### 4.4.17 32点阵不等宽ASCII白正（Times New Roman）字符
- 说明：
  - ASCIICode：表示 ASCII码（8bits）
  - BaseAdd：说明该套字库在芯片中的起始地址。
  - Address：ASCII字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x08F340
  - if (ASCIICode >= 0x20) and (ASCIICode <= 0x7E) then
    - Address = (ASCIICode –0x20 ) * 130 + BaseAdd

#### 4.4.18 14x28点阵数字符号字符
- 说明：此部分内容为0 1 2 3 4 5 6 7 8 9 , . ￥ $ £  
- Squence：表示 字符顺序，从0开始计数。
- BaseAdd：说明该套字库在芯片中的起始地址。
- Address：对应字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x092400
  - Address = Squence * 56+ BaseAdd

#### 4.4.19 20x40点阵数字符号字符
- 说明：此部分内容为0 1 2 3 4 5 6 7 8 9 . ,  
- Squence：表示 字符顺序，从0开始计数。
- BaseAdd：说明该套字库在芯片中的起始地址。
- Address：对应字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x092748
  - Address = (Squence ) * 120+BaseAdd

#### 4.4.20 28点阵不等宽数字符号字符
- 说明：此部分内容为0 1 2 3 4 5 6 7 8 9 , . ￥ $ £
- Squence：表示 字符顺序，从0开始计数。
- BaseAdd：说明该套字库在芯片中的起始地址。
- Address：对应字符点阵在芯片中的字节地址。
- 注：前两个字节为宽度信息。
- 计算方法：
  - BaseAdd=0x092CE8
  - Address = Squence * 114+ BaseAdd

#### 4.4.21 40点阵不等宽数字符号字符
- 说明：此部分内容为0 1 2 3 4 5 6 7 8 9 . ,  
- Squence：表示 字符顺序，从0开始计数。
- BaseAdd：说明该套字库在芯片中的起始地址。
- Address：对应字符点阵在芯片中的字节地址。
- 计算方法：
  - BaseAdd=0x93396
  - Address = Squence * 202+ BaseAdd

#### 4.4.22 EAN13条形码调用程序
- 函数：DWORD* BAR_CODE(int* BAR_NUM)
- 功能：将数组条形码转为对应条形码图形地址。
- 参数：int* BAR_NUM 条形码数字数组指针，BAR_NUM[13]数组包含13个数字。
- 返回：定义DWORD BAR_PIC_ADDR[13];用于存放对应地址，返回此数组指针。

```c
DWORD BAR_PIC_ADDR[13];
DWORD* BAR_CODE(int* BAR_NUM)
{
 DWORD i,BaseAddr=0x3F8FD2;
BAR_PIC_ADDR[0]=BAR_NUM[0]*54+540*0+ BaseAddr;
BAR_PIC_ADDR[1]=BAR_NUM[1]*54+540*1+ BaseAddr;
 
 switch(BAR_NUM[0])
 {
 case 0:
  for(i=2;i<=6;i++)
  {
   BAR_PIC_ADDR[i]=BAR_NUM[i]*54+540*1+ BaseAddr;  
}
break;
   
 case 1:
  BAR_PIC_ADDR[2]=BAR_NUM[2]*54+540*1+ BaseAddr;    
  BAR_PIC_ADDR[3]=BAR_NUM[3]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[4]=BAR_NUM[4]*54+540*1+ BaseAddr;
  BAR_PIC_ADDR[5]=BAR_NUM[5]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[6]=BAR_NUM[6]*54+540*2+ BaseAddr;
  break;
 case 2:
  BAR_PIC_ADDR[2]=BAR_NUM[2]*54+540*1+ BaseAddr;    
  BAR_PIC_ADDR[3]=BAR_NUM[3]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[4]=BAR_NUM[4]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[5]=BAR_NUM[5]*54+540*1+ BaseAddr;
  BAR_PIC_ADDR[6]=BAR_NUM[6]*54+540*2+ BaseAddr;   
  break;
 case 3:
  BAR_PIC_ADDR[2]=BAR_NUM[2]*54+540*1+ BaseAddr;    
  BAR_PIC_ADDR[3]=BAR_NUM[3]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[4]=BAR_NUM[4]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[5]=BAR_NUM[5]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[6]=BAR_NUM[6]*54+540*1+ BaseAddr;  
  break;
   


case 4:
  BAR_PIC_ADDR[2]=BAR_NUM[2]*54+540*2+ BaseAddr;    
  BAR_PIC_ADDR[3]=BAR_NUM[3]*54+540*1+ BaseAddr;
  BAR_PIC_ADDR[4]=BAR_NUM[4]*54+540*1+ BaseAddr;
  BAR_PIC_ADDR[5]=BAR_NUM[5]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[6]=BAR_NUM[6]*54+540*2+ BaseAddr;
  break;
 case 5:
  BAR_PIC_ADDR[2]=BAR_NUM[2]*54+540*2+ BaseAddr;    
  BAR_PIC_ADDR[3]=BAR_NUM[3]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[4]=BAR_NUM[4]*54+540*1+ BaseAddr;
  BAR_PIC_ADDR[5]=BAR_NUM[5]*54+540*1+ BaseAddr;
  BAR_PIC_ADDR[6]=BAR_NUM[6]*54+540*2+ BaseAddr;
  break;
 case 6:
  BAR_PIC_ADDR[2]=BAR_NUM[2]*54+540*2+ BaseAddr;    
  BAR_PIC_ADDR[3]=BAR_NUM[3]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[4]=BAR_NUM[4]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[5]=BAR_NUM[5]*54+540*1+ BaseAddr;
  BAR_PIC_ADDR[6]=BAR_NUM[6]*54+540*1+ BaseAddr;
  break;
 case 7:
  BAR_PIC_ADDR[2]=BAR_NUM[2]*54+540*2+ BaseAddr;    
  BAR_PIC_ADDR[3]=BAR_NUM[3]*54+540*1+ BaseAddr;
  BAR_PIC_ADDR[4]=BAR_NUM[4]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[5]=BAR_NUM[5]*54+540*1+ BaseAddr;
  BAR_PIC_ADDR[6]=BAR_NUM[6]*54+540*2+ BaseAddr;
  break;
 case 8:
  BAR_PIC_ADDR[2]=BAR_NUM[2]*54+540*2+ BaseAddr;    
  BAR_PIC_ADDR[3]=BAR_NUM[3]*54+540*1+ BaseAddr;
  BAR_PIC_ADDR[4]=BAR_NUM[4]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[5]=BAR_NUM[5]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[6]=BAR_NUM[6]*54+540*1+ BaseAddr;
  break;
 case 9:
  BAR_PIC_ADDR[2]=BAR_NUM[2]*54+540*2+ BaseAddr;    
  BAR_PIC_ADDR[3]=BAR_NUM[3]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[4]=BAR_NUM[4]*54+540*1+ BaseAddr;
  BAR_PIC_ADDR[5]=BAR_NUM[5]*54+540*2+ BaseAddr;
  BAR_PIC_ADDR[6]=BAR_NUM[6]*54+540*1+ BaseAddr;
  break;
   
 }
 

BAR_PIC_ADDR[7]=BAR_NUM[7]*54+540*3+ BaseAddr;
 for(i=8;i<=11;i++)
 {
  BAR_PIC_ADDR[i]=BAR_NUM[i]*54+540*4+ BaseAddr;  
 }
 BAR_PIC_ADDR[12]=BAR_NUM[12]*54+540*5+ BaseAddr;
return BAR_PIC_ADDR;
}
```

## 5. 编码转换要求

### 5.1 转换方案
由于 ESP32-S3 的 RAM 有限，建议采用以下任一方案：
- 方案 A：使用 ESP-IDF 内置的 iconv 库进行 UTF-8 ↔ GBK 转换（需启用 CONFIG_LIBC_ICONV）。
- 方案 B：预先生成常用汉字的 Unicode ↔ GBK 映射表（如使用 u8g2 的编码表），仅转换出现的字符，减少内存占用。
- 方案 C：若消息内容固定，可提前将 UTF-8 消息转换为 GBK 后存储，避免运行时转换。

### 5.2 转换函数接口
```c
// 将 UTF-8 字符串转换为 GBK 编码，输出到缓冲区
int utf8_to_gbk(const char *utf8_str, char *gbk_buf, size_t gbk_buf_size);
```

## 6. 显示要求

### 6.1 SSD1306 驱动
- 接口类型：SPI
- 引脚配置：
  - SCLK: 4
  - MOSI: 5
  - DC: 6
  - CS: 1
  - RES: 2
- 实现基本绘图函数：画点、画矩形、显示位图。
- 提供帧缓冲区（可选，建议使用 128×64 / 8 = 1024 字节的显存）。

### 6.2 字符绘制
- 根据当前字体大小，从字库中读取点阵数据（每个字符的点阵字节数：(width * height) / 8）。
- 将点阵数据按位绘制到屏幕缓冲区，支持 x、y 坐标指定。
- 支持 ASCII 字符（如英文字母、数字、标点），使用字库中的 ASCII 部分或单独的内置 ASCII 点阵。

### 6.3 字符串绘制
- 逐字符绘制，遇到非 ASCII（GBK 双字节）时合并为完整汉字。
- 自动处理换行：若当前 x 坐标 + 字符宽度 > 屏幕宽度，则换到下一行（y 坐标增加字符高度）。
- 支持文本对齐（左对齐、居中、右对齐）可选。

## 7. 存储与资源约束
- 字库文件大小：约 2~4 MB，需烧录至外部 Flash（例如分区 font，地址自定）。
- RAM 占用：字库无需加载到 RAM，仅需少量缓冲区（如 1KB）用于存储当前显示的点阵和转换缓冲区。
- 代码大小：控制在 64KB 以内。

## 8. 实现注意事项
- 字库偏移验证：必须严格对照芯片手册，确认偏移公式和字节数，否则可能显示错误字符。
- ASCII 字符处理：如果字库中 ASCII 字符的存储区域与汉字不同，需单独处理；或使用内置的 ASCII 点阵（占用极少 ROM）。
- Flash 读取优化：避免频繁的小字节读取，建议批量读取一行中的多个字符点阵，减少 SPI Flash 访问次数。
- 字体大小切换：如果字库仅提供一种大小（如 16×16），则其他大小需通过软件缩放实现（如 12×12 裁剪，24×24 双倍放大），但会牺牲显示质量。更佳方案是确认字库是否内置多种大小。

### 8.1 WiFi连接实现注意事项
- **SmartConfig实现**：
  - 使用ESP-IDF的esp_smartconfig组件实现SmartConfig功能
  - SmartConfig类型：支持ESPTOUCH和AIRKISS两种协议
  - 超时设置：建议设置合理的超时时间（如60秒），超时后自动停止SmartConfig
  - 错误处理：处理SmartConfig过程中的各种错误状态
  
- **WiFi配置文件管理**：
  - 配置文件路径：`/spiffs/wifi_config.json`
  - 文件操作：使用SPIFFS API进行读写操作
  - 错误处理：处理文件不存在、读写失败等异常情况
  - 安全性：WiFi密码以明文存储在配置文件中，注意保护SPIFFS分区安全
  
- **WiFi连接流程**：
  1. 系统启动，初始化WiFi驱动
  2. 读取SPIFFS中的WiFi配置文件
  3. 如果配置文件存在，尝试连接WiFi
  4. 如果连接成功，显示连接状态和IP地址
  5. 如果连接失败或配置文件不存在，启动SmartConfig
  6. SmartConfig接收WiFi信息后，连接WiFi
  7. 连接成功后，保存WiFi信息到SPIFFS
  8. 监控WiFi连接状态，断线时自动重连
  
- **WiFi状态监控**：
  - 使用ESP-IDF的WiFi事件处理机制
  - 监听WiFi事件：STA_START、STA_CONNECTED、STA_DISCONNECTED等
  - 实现自动重连机制：断线后延迟重连
  - 记录连接日志，便于调试
  
- **OLED显示集成**：
  - WiFi状态变化时，更新OLED显示内容
  - 显示WiFi连接状态、信号强度、IP地址等信息
  - SmartConfig模式下，显示配置提示信息
  - 使用中文字库显示WiFi相关提示信息

### 测试用例：
- 输入包含中文、英文、数字、标点的混合字符串。
- 切换字体大小后验证显示效果。
- 长字符串自动换行测试。

## 9. 交付物
完整的 ESP-IDF 工程源码，包含：
- 字库分区定义及读取模块
- UTF-8 → GBK 转换模块
- SSD1306 驱动（含 I²C/SPI 接口）
- 汉字显示模块（支持三种字体大小）
- 示例应用：从 MQTT 接收字符串并显示
- 简要说明文档（README），描述编译、烧录步骤及 API 使用方法。

## 版本历史

| 版本 | 日期       | 描述     | 作者 |
|------|------------|----------|------|
| 1.0  | 2026-03-29 | 初始版本 | -    |

## 10. 参考工具

### 10.1 汉字字模提取工具
- **网址**: https://www.23bei.com/tool/216.html
- **功能**: LCD/OLED汉字字模提取软件，支持HZK16宋体GB2312中文16*16点阵字库
- **特点**: 
  - 支持多种取模方式（横向8点左高位、横向8点右高位、纵向8点上高位、纵向8点下高位等）
  - 自动滤除重复汉字，生成无重复汉字库
  - 支持C51格式或ASM格式数组输出
  - 可生成索引库，实现特定字符串的显示

## 11. 从字库读取数据并显示到SSD1306

### 11.1 技术说明文档
- **文档路径**: [fontFromZk.md](./fontFromZk.md)
- **功能**: 详细说明如何从GT32L24M0140字库文件中读取汉字点阵数据，并将其正确显示到SSD1306 OLED屏幕上
- **内容包括**:
  - SPIFFS分区配置和字库文件挂载
  - 字库地址计算方法（12/16/24字体）
  - 横向取模到纵向取模的数据转换
  - SSD1306显示函数实现
  - 注意事项和故障排除
  - 完整的使用示例

### 11.2 快速开始
如需快速了解如何使用字库显示中文，请参考 [fontFromZk.md](./fontFromZk.md) 文档中的详细说明和示例代码。
