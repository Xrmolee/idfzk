import struct

def generate_uni2gbk_bin():
    # 创建一个 128KB 的空字节数组 (0x0000 到 0xFFFF，每个 2 字节)
    # 足够覆盖整个基本多语言平面 (BMP)
    mapping = bytearray(65536 * 2)

    for i in range(65536):
        try:
            char = chr(i)
            # 尝试转为 GBK
            gbk_bytes = char.encode('gbk')
            
            # GBK 可能是 1 字节(ASCII) 或 2 字节(中文)
            if len(gbk_bytes) == 2:
                # 存储为大端或小端，建议与 ESP32 读取习惯一致（这里用大端）
                mapping[i*2] = gbk_bytes[0]
                mapping[i*2 + 1] = gbk_bytes[1]
            elif len(gbk_bytes) == 1:
                mapping[i*2] = 0x00
                mapping[i*2 + 1] = gbk_bytes[0]
        except UnicodeEncodeError:
            # GBK 不支持该字符，填充 0
            mapping[i*2] = 0x00
            mapping[i*2 + 1] = 0x00

    with open("uni2gbk.bin", "wb") as f:
        f.write(mapping)
    print("生成成功: uni2gbk.bin (128KB)")

if __name__ == "__main__":
    generate_uni2gbk_bin()