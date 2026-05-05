# ESP12F_Fan_4P

ESP12F_Fan_4P 是一个基于 ESP8266 ESP-12F 的四线 PWM 风扇控制器，用于壁炉烟囱正压送风场景。设备通过 Web 页面、本地按键和红外遥控控制风扇转速与定时运行，并使用 Esp8266Base 提供 WiFi、Web、配置、日志、NTP、mDNS、看门狗和低功耗能力。

默认主机名为 `esp-fan`，配网后可访问：

- `http://esp-fan.local/fan`
- `http://esp-fan.local/config`
- `http://esp-fan.local/logs`

默认 Web 登录用户为 `admin`，默认密码为 `admin123`。密码可以在配置页面修改。

## 功能

- 0-100% PWM 调速，25 kHz 输出，支持最低有效转速限制。
- 四线风扇转速反馈采集，支持无 RPM 堵转检测和保护。
- 软启动、软停止，时间可配置。
- 定时运行，支持常用预设和自定义时间。
- 本地加速/减速按键控制。
- 红外遥控学习，支持加速、减速、停止和定时快捷键。
- 移动端兼容的 Web 控制、配置和日志查看页面。
- 上电恢复，可恢复上次转速和剩余定时。
- 风扇停止一段时间后进入 Modem Sleep，保持 WiFi 和 Web 服务可用。
- BOOT 键长按 1 秒清除 WiFi 凭证并重启。
- 文件日志持久化，使用 Esp8266Base 的 4 文件滚动日志。

## 硬件

目标硬件为 ESP-12F / ESP-12E，4 MB Flash。当前引脚分配：

| 功能 | GPIO | NodeMCU 标号 |
|------|------|--------------|
| 风扇 PWM | GPIO5 | D1 |
| 风扇 TACH | GPIO12 | D6 |
| 加速按键 | GPIO14 | D5 |
| 减速按键 | GPIO4 | D2 |
| 板载 LED | GPIO2 | D4 |
| 红外接收 | GPIO13 | D7 |
| BOOT / 清除 WiFi | GPIO0 | D3 |

注意：未接入真实风扇时，设置非零速度后可能触发堵转保护，因为 TACH 没有 RPM 反馈。这是保护逻辑的正常表现。

## Web 页面

`/fan` 是主控制页，显示目标速度、实际输出、定时倒计时、运行时长、IP、RSSI、时钟和堵转状态。页面提供速度预设、自定义速度、定时预设、自定义定时、取消定时和停止风扇操作。

`/config` 是配置页，可修改最低速度、休眠等待时间、软启动、软停止、堵转检测时间、上电恢复策略和管理员密码，也提供红外学习入口。

`/logs` 使用 Esp8266Base 内置日志页面，读取 `/logs/app.log` 及滚动日志文件。一次新启动会有启动分割和启动次数，NTP 同步后日志时间使用真实日期时间。

Esp8266Base 还提供 WiFi、重启等基础管理页面；OTA 页面是否可用取决于编译配置。

## HTTP API

所有 API 由 Esp8266Base Web 鉴权保护。

| 路径 | 方法 | 作用 |
|------|------|------|
| `/api/status` | GET | 获取风扇状态、网络状态、时钟和运行信息 |
| `/api/speed` | GET / POST | 读取或设置速度，POST 参数 `speed=0..100` |
| `/api/timer` | GET / POST | 读取或设置定时，POST 参数 `seconds=0..356400` |
| `/api/stop` | POST | 停止风扇并清除定时 |
| `/api/config` | GET / POST | 读取或保存配置参数 |
| `/api/ir/learn` | POST | 启动红外学习，POST 参数 `key_index=0..5` |

由于 Esp8266Base 当前配置的应用 API 配额为 6 个，本项目只注册以上 6 个 API。红外学习状态由学习请求结果和日志观察，不单独占用一个公开 API。

## 日志与配置审计

本项目直接使用 Esp8266Base 的文件日志能力：

- 日志路径：`/logs/app.log`
- 单文件大小：16 KB
- 滚动文件数：4
- 总保留量：约 64 KB

启动时会启用：

- 文件日志写入
- 配置写入审计
- 配置读取审计开关

默认固件使用 DEBUG 日志等级，便于现场调试。配置读取审计由 Esp8266Base 的 `ESP8266BASE_CFG_READ_AUDIT_LEVEL` 单独控制；不定义该宏时沿用基础库默认行为。配置审计不做脱敏，便于个人项目调试观察。用户操作、配置保存、WARN 及以上级别日志都会进入文件日志。

## 配置项

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `fan_min_speed` | `10` | 最低有效速度，1-9% 会提升到此值 |
| `fan_sleep_wait` | `60` | 风扇停止后进入 Modem Sleep 的等待秒数 |
| `fan_soft_start` | `1000` | 软启动时间，毫秒 |
| `fan_soft_stop` | `1000` | 软停止时间，毫秒 |
| `fan_block_detect` | `1500` | 无 RPM 持续多久判定堵转，毫秒 |
| `fan_auto_restore` | `true` | 上电后是否恢复上次速度和定时 |
| `fan_web_pass` | `admin123` | Web 管理密码 |

运行状态还会持久化上次速度、剩余定时、累计运行时长和红外学习码。

## 构建与烧录

项目使用 PlatformIO。

```bash
pio run -e esp12f
pio run -e esp12f -t upload
pio device monitor -e esp12f
```

运行本地单元测试：

```bash
pio test -e native
```

如需临时强制观察所有配置读取日志，可显式加入：

```ini
-DESP8266BASE_LOG_LEVEL=0
-DESP8266BASE_CFG_READ_AUDIT_LEVEL=0
```

## 依赖

- Esp8266Base：`https://github.com/tangyangguang/Esp8266Base`
- IRremoteESP8266：`crankyoldgit/IRremoteESP8266@^2.8.6`

`platformio.ini` 使用 Esp8266Base 提供的 4 MB Flash / 2 MB 文件系统 linker script，便于保存滚动日志。

## 目录

```text
src/
  main.cpp                 引脚、模块实例和 Esp8266Base 初始化
  fan/                     风扇驱动、按键、LED、红外和控制状态机
  web/                     风扇专属 Web 页面和 API
docs/                      需求、硬件、软件架构和代码设计文档
test/                      native 单元测试和 Esp8266Base 测试替身
```
