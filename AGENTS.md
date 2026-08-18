# WiFi_Web_Template

## 工程定位

这是面向 `ESP32-S3-WROOM-1-N16R8` 的本地 Wi-Fi 网页交互模板，使用 ESP-IDF `v6.0.2`。设备通过临时配置热点完成网页配网，连接手机热点或路由器后，在局域网内提供中文控制台、REST API 和 WebSocket 实时状态。

当前示例使用 GPIO48 驱动板载单颗 WS2812B，用于演示网页到真实硬件的开关、颜色和亮度控制。完整 GPIO、供电、Flash 和 PSRAM 约束见 [`doc/HARDWARE.md`](doc/HARDWARE.md)；修改硬件相关代码前必须先阅读该文件。

## 固定环境

- 芯片：ESP32-S3-WROOM-1-N16R8
- Flash：16 MB QSPI
- 从本模板创建工程时，应完整利用板载 16 MB Flash，并根据实际功能按需设置分区。
- PSRAM：8 MB Octal
- ESP-IDF：`v6.0.2`
- 构建目标：`esp32s3`
- ESP-IDF 路径：由 `tools/idf.ps1` 根据本机环境解析
- 工具路径：由 `tools/idf.ps1` 根据本机环境解析

AI、自动化任务和普通 PowerShell 必须统一通过工程内的 `tools/idf.ps1` 调用 ESP-IDF。该脚本会优先使用 `ESP_IDF_POWERSHELL_PROFILE` 或 `IDF_TOOLS_PATH` 指定的本机环境，并切换到工程根目录；不要依赖 VS Code 终端状态或预先手动执行激活脚本。

```powershell
.\tools\idf.ps1 --version                 # 应输出 ESP-IDF v6.0.2
.\tools\idf.ps1 set-target esp32s3
.\tools\idf.ps1 menuconfig
.\tools\idf.ps1 build
.\tools\idf.ps1 -p COMx flash monitor     # COMx 替换为 CH340 串口，Ctrl+] 退出
```

不带参数执行 `.\tools\idf.ps1` 时默认构建工程。

项目设置位于 `Component config > WiFi Web Template`：

- mDNS 主机名默认为 `esp32s3-web`。
- 配置热点密码默认为 `esp32setup`，长度必须为 8–63 个字符。
- 已保存网络连接失败 30 秒后开启配置热点。
- 新网络验证超时默认为 20 秒。

## 核心行为与不可破坏的约束

### 配网与凭据

- 没有凭据时立即开放 `ESP32S3-Setup-XXXX`，其中 `XXXX` 为 SoftAP MAC 末四位。
- 配置热点固定使用 `192.168.4.1`，启用 WPA2、DNS 重定向和常见系统的 Captive Portal 探测路由。
- Wi-Fi 驱动必须保持 `WIFI_STORAGE_RAM`。候选 SSID/密码只有在 STA 成功获取 DHCP 地址后才能写入自有 NVS。
- 候选网络验证失败或超时时，不得覆盖旧凭据；有旧凭据时必须恢复旧配置。
- 不得在日志、HTTP 响应或状态 JSON 中输出 Wi-Fi 密码。
- 断网重连退避为 1、2、4、8、16、30 秒，之后保持 30 秒间隔持续尝试，不因断网重启设备。
- 已保存网络连续失败达到配置时间后进入 AP+STA；联网成功约 5 秒后关闭配置热点并回到纯 STA。

### 网页服务

- HTML、CSS、JavaScript 必须嵌入固件，不得依赖 CDN、互联网字体或外部服务。
- 配网页面支持扫描列表、开放网络、隐藏 SSID、连接进度和明确的失败提示。
- 业务页以移动端为优先，显示 SSID、IP、RSSI、空闲堆、运行时间、WebSocket 客户端数和趋势图。
- WebSocket 每秒推送完整状态；REST 写入成功后应立即广播，多个浏览器必须保持同步。
- 所有 JSON 写接口必须限制请求体大小并校验字段存在性、类型、长度和数值范围；错误输入返回 HTTP 4xx，不能导致设备重启。
- 业务网页当前没有登录和 HTTPS，只能定位为可信局域网模板。不要在文档中描述为可直接暴露到公网的安全方案。

### GPIO 与运行期状态

- WS2812B 固定使用 GPIO48、RMT TX 和 GRB 字节顺序。
- GPIO0 是 BOOT/Strapping 引脚，只能在启动完成后检测长按。长按约 5 秒清除 Wi-Fi 凭据并进入配网；不要改变复位阶段的 Strapping 电平。
- RGB 灯状态和示例文本只保存在 RAM，重启后恢复默认值，避免频繁写 Flash。
- 网络、灯光、文本及 WebSocket 广播涉及多任务访问时必须保持互斥保护。

## 工程结构

- `components/main/app_main.c`：应用入口，只负责 NVS、各业务组件和回调的启动顺序。
- `components/wifi_manager/`：Wi-Fi STA/AP 状态机、重连、mDNS 和网络扫描；`wifi_credentials.c/.h` 私有负责 NVS 凭据事务。
- `components/web_ui/web_server.c`：HTTP 生命周期、嵌入资源、Captive Portal 路由和 WebSocket 广播。
- `components/web_ui/web_api.c`：REST 处理器、输入校验与统一状态 JSON。
- `components/web_ui/web/`：中文配网页面和业务控制台资源。
- `components/ws2812_led/`：GPIO48 WS2812B 的公开控制接口与私有 RMT 编码器。
- `components/provision_button/`：GPIO0 BOOT 长按检测，通过回调触发重新配网。
- `components/dns_server/`：基于 ESP-IDF 官方 Captive Portal 示例的 DNS 重定向组件。
- `components/main/Kconfig.projbuild`：主机名、配置热点密码和超时参数。
- `sdkconfig.defaults`：ESP32-S3 Flash、Octal PSRAM 和 WebSocket 默认配置。

所有自有代码均放在 `components/`，应用入口组件为 `components/main/`，工程根目录不再单独设置 `main/`。为减少目录嵌套，C 实现和私有头文件直接放在各组件根目录，只有供其他组件使用的稳定公共头文件放在 `include/`。网页资源保留在 `web/`。不要把私有头文件移入公共 `include/`。组件间通过 `PRIV_REQUIRES` 显式声明依赖，不得依靠隐式传递包含路径。托管依赖分别由组件内的 `idf_component.yml` 和根目录 `dependencies.lock` 固定，目前包括 Espressif cJSON 与 mDNS。不要直接修改 `managed_components/`。

## 后续拓展方式

- 新增传感器或执行器时，新建独立组件并提供最小公开头文件；不要把驱动实现塞回 `app_main.c`。
- 新增 REST 路由和状态字段时修改 `components/web_ui/web_api.c`；HTTP 服务生命周期、静态资源和 WebSocket 连接管理保留在 `web_server.c`。
- 新增网页资源时放入 `components/web_ui/web/`，同时加入该组件 `CMakeLists.txt` 的 `EMBED_FILES`。
- 新增网络策略时只修改 `wifi_manager`；NVS 键和值的读写保持在私有 `wifi_credentials` 模块。
- 若状态模型继续增长，应新增独立的应用状态组件，再由 `web_api` 读取快照，不要让硬件组件依赖 HTTP 层。

## 配网和访问流程

1. 手机连接 `ESP32S3-Setup-XXXX`，默认密码为 `esp32setup`。
2. 系统通常会自动弹出配网页；未弹出时访问 <http://192.168.4.1/>。
3. 选择或输入 Wi-Fi，填写密码并提交。设备先在 RAM 中试连，成功获取 IP 后才保存。
4. 页面显示目标 IP 后，配置热点约 5 秒后关闭。
5. 将手机切换到目标网络，访问串口日志中的 IP 或 <http://esp32s3-web.local/>。

手机自身作为热点时，部分系统无法解析热点客户端的 mDNS。此时从手机热点客户端列表或串口日志获取 ESP32 的 IP。

正常联网时可以通过业务页重新配网。网页不可用时，在固件已经运行的情况下长按 BOOT 约 5 秒。不要在上电或复位时持续按住 BOOT，否则会进入下载模式。

## HTTP 和 WebSocket 接口

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| `GET` | `/api/wifi/scan` | 扫描附近网络 |
| `POST` | `/api/wifi/connect` | 提交 `{"ssid":"...","password":"..."}` 并异步验证 |
| `GET` | `/api/wifi/state` | 查询配网、验证和联网状态 |
| `POST` | `/api/wifi/reprovision` | 清除凭据并进入配网模式 |
| `GET` | `/api/status` | 获取完整设备状态 |
| `PUT` | `/api/led` | 设置 `{"on":true,"color":"#2A7CFF","brightness":60}` |
| `PUT` | `/api/message` | 设置 `{"message":"文本"}`，最多 96 字节 |
| `GET` | `/ws` | 升级为 WebSocket 并接收状态推送 |

修改状态 JSON 时必须同步检查业务页面字段、`/api/status`、`/api/wifi/state` 和 WebSocket 消费端，避免接口漂移。

## 开发与验收要求

每次修改至少执行：

```powershell
.\tools\idf.ps1 build
```

涉及网页脚本时额外执行 JavaScript 语法检查，并确认下列资源均能加载：

- `/`
- `/app.css`
- `/app.js`
- `/portal.js`

涉及网络状态机时，真机至少覆盖：

- 首次启动无凭据，配置热点和 Captive Portal 可访问。
- 开放网络、WPA2 网络、隐藏 SSID 和错误密码。
- 错误候选凭据不会覆盖原有可用网络。
- 目标热点关闭后自动回退配网，恢复后能重新连接。
- 配网成功后 NVS 保存生效，掉电重启能够恢复。
- 网页按钮和 BOOT 长按均能重新配网。
- 手机热点模式及手机与设备同路由器模式均可访问。
- 多个浏览器的灯光、文本和状态能够实时同步。
- 畸形 JSON、非法颜色、越界亮度和超长文本返回 HTTP 4xx。

烧录前确认串口确实属于 CH340 开发板，不要把蓝牙虚拟 COM 口当作目标端口。

## 官方资料

遇到 ESP-IDF 接口或版本问题时，优先查阅本机与 `v6.0.2` 完全匹配的资料：

- `$IDF_PATH\components`：API 头文件、实现和 Kconfig。
- `$IDF_PATH\examples`：官方初始化和组件用法示例。
- `$IDF_PATH\docs\zh_CN`、`$IDF_PATH\docs\en`：编程指南和 API 文档。
- ESP-IDF 工具目录：工具链、Python、CMake、OpenOCD 和环境脚本，仅用于构建环境排障。

## 安全说明

- Wi-Fi 凭据保存在普通 NVS；未启用 Flash 加密时不属于加密存储。
- 配置热点使用 WPA2，但业务接口是无认证的明文 HTTP。
- 首版不包含 HTTPS、云服务、OTA、BLE 配网或静态 IP，不要在未明确扩大需求时擅自加入这些功能。
