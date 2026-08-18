# ESP32-S3 开发板硬件约束

## 固定配置

- 模组：ESP32-S3-WROOM-1-N16R8
- 存储：16 MB QSPI Flash、8 MB Octal PSRAM
- GPIO：3.3V 逻辑，禁止接入 5V 逻辑电平
- ESP-IDF 目标：`esp32s3`

```text
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
```

## 外部排针

### H1

| 引脚 | 网络 | 固定连接或限制 |
| ---: | --- | --- |
| 1 | 3V3 | 板载 3.3V |
| 2 | 3V3 | 板载 3.3V |
| 3 | CHIP_PU | RST，低电平复位 |
| 4 | GPIO4 | - |
| 5 | GPIO5 | - |
| 6 | GPIO6 | - |
| 7 | GPIO7 | - |
| 8 | GPIO15 | - |
| 9 | GPIO16 | - |
| 10 | GPIO17 | UART1 TX |
| 11 | GPIO18 | UART1 RX |
| 12 | GPIO8 | - |
| 13 | GPIO3 | J5 导通，10k 上拉；Strapping/JTAG；禁止 ADC |
| 14 | GPIO46 | Strapping，默认低；复位时勿外部拉高 |
| 15 | GPIO9 | - |
| 16 | GPIO10 | - |
| 17 | GPIO11 | - |
| 18 | GPIO12 | - |
| 19 | GPIO13 | - |
| 20 | GPIO14 | - |
| 21 | 5V-IN | 默认输入；J1 短接后可向外输出 5V |
| 22 | GND | 地 |

### H2

| 引脚 | 网络 | 固定连接或限制 |
| ---: | --- | --- |
| 1 | GND | 地 |
| 2 | U0TXD / GPIO43 | CH340C TX、板载 TX 灯 |
| 3 | U0RXD / GPIO44 | CH340C RX、板载 RX 灯 |
| 4 | GPIO1 | - |
| 5 | GPIO2 | - |
| 6 | GPIO42 | JTAG MTMS |
| 7 | GPIO41 | JTAG MTDI |
| 8 | GPIO40 | JTAG MTDO |
| 9 | GPIO39 | JTAG MTCK |
| 10 | GPIO38 | - |
| 11 | GPIO37 | **禁止使用：Octal PSRAM 占用** |
| 12 | GPIO36 | **禁止使用：Octal PSRAM 占用** |
| 13 | GPIO35 | **禁止使用：Octal PSRAM 占用** |
| 14 | GPIO0 | BOOT/自动下载；Strapping，低电平进入下载模式 |
| 15 | GPIO45 | Strapping，默认低；复位时勿外部拉高 |
| 16 | GPIO48 | J3 导通，连接 WS2812B DIN |
| 17 | GPIO47 | - |
| 18 | GPIO21 | - |
| 19 | GPIO20 | USB-OTG D+ |
| 20 | GPIO19 | USB-OTG D- |
| 21 | GND | 地 |
| 22 | GND | 地 |

## 其他约束

- Strapping 复位电平：GPIO0/GPIO3 为高，GPIO45/GPIO46 为低；外设不得反向驱动。GPIO3 复位后可作数字 IO。
- 启用 Wi-Fi 时避免使用 ADC2，优先使用 ADC1。
- USB-ISP 使用 CH340C 和 DTR/RTS 自动下载电路；USB-OTG 数据线固定为 GPIO20/GPIO19。
- 板载 3.3V 输出能力约 1A（含开发板自身消耗）。J1 短接后不要再从 H1-21 接入其他 5V 电源。
