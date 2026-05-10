# ESP12F_Fan_4P

ESP12F_Fan_4P 是一个基于 ESP8266 ESP-12F 的四线 PWM 风扇控制器，用于壁炉烟囱正压送风场景。设备通过 Web 页面、本地按键和红外遥控控制风扇转速与定时运行，并使用 Esp8266Base 提供主入口协调、串口/文件日志、LittleFS 配置存储、WiFi STA/AP 配网、Web 管理页、Web OTA、NTP、mDNS、Modem/Deep Sleep 封装和 Watchdog 能力。

默认主机名为 `esp-fan`，配网后可访问：

- `http://esp-fan.local/fan`
- `http://esp-fan.local/config`
- `http://esp-fan.local/esp8266base`
- `http://esp-fan.local/wifi`
- `http://esp-fan.local/ota`
- `http://esp-fan.local/logs`
- `http://esp-fan.local/auth`
- `http://esp-fan.local/reboot`
- `http://esp-fan.local/health`

默认 Web 登录用户为 `admin`，默认密码为 `admin123`。密码可以在 `/auth` 页面修改。

## 功能

- 0-100% PWM 调速，25 kHz 输出，支持最低有效转速限制。
- 四线风扇转速反馈采集，支持基于 RPM=0 持续时间的堵转检测和保护。
- 软启动、软停止，时间可配置。
- 定时运行，支持常用预设和自定义时间。
- 本地加速/减速按键控制。
- 红外遥控学习，支持加速、减速、停止和 30min/1h/2h/4h/8h 定时快捷键。
- 移动端兼容的 Web 控制、配置和日志查看页面。
- 上电恢复，可恢复上次转速和剩余定时。
- 风扇停止一段时间后进入 Modem Sleep，保持 WiFi 和 Web 服务可用。
- BOOT 键长按 1 秒清除 WiFi 凭证并重启。
- 加速键和减速键同时长按 5 秒会执行出厂重置，清除风扇配置、WiFi 凭证和 Web 密码后重启。
- 板载 LED 支持 0-4 档亮度、WiFi 断连慢闪、故障快闪和有效操作反馈闪烁。
- 文件日志持久化，使用 Esp8266Base 的 4 文件滚动日志。
- OTA 固件升级，使用 Esp8266Base 内置 `/ota` 上传页和 POST 上传处理。

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

## LED 指示灯

板载 LED 位于 GPIO2，低电平亮。常规运行时，0 档熄灭，1-4 档分别对应 25%/50%/75%/100% 亮度。

LED 状态优先级固定为：故障快闪（5Hz） > WiFi 未连接慢闪（1Hz） > 档位亮度/熄灭。所有有效用户操作会闪 1 下作为反馈，包括 Web 调速、停止、定时、配置保存且有变化、按键有效加减档、红外有效操作和红外学习成功；默认反馈时长为 200ms，可在配置页设置为 0-2000ms，0 表示关闭操作反馈闪烁。操作反馈采用反相脉冲：当前亮时短暂熄灭，当前灭时短暂点亮，因此 100% 亮度下也能看到一次反馈。非法参数、配置无变化、未知红外码、边界档位重复加减、学习超时不触发反馈。故障状态下反馈闪烁不会清除快闪，只有恢复成功或停止清除故障后才回到普通 LED 规则。

## Web 页面

`/fan` 是主控制页，显示状态（堵转会合并显示为 `Error / Blocked`）、目标速度、实际输出、档位、RPM、定时倒计时、运行时长、RSSI、日期和时间。页面提供速度预设、自定义速度、定时预设、自定义定时、取消定时和停止风扇操作。

`/config` 是配置页，可修改最低速度、休眠等待时间、软启动、软停止、堵转检测时间、操作反馈闪烁时长和上电恢复策略，也提供红外学习入口。红外学习区由服务端分块渲染所有命令，并显示每个命令是否已学习、协议号和红外码；每个命令可单独学习或清除。同一个红外码只能绑定一个命令，已有重复绑定会标记为 Duplicate。配置保存成功会显示 changed 数；无变化时仅显示 no changes，不触发 LED 操作反馈。

`/esp8266base` 是 Esp8266Base 内置系统首页，显示 Network、Device、Time 等基础状态，包括 Hostname、WiFi、SSID、IP、RSSI、MAC、固件版本、Boot count、Chip ID、CPU、Flash、Sketch、OTA free、Uptime、NTP 和 Boot time。

`/wifi` 是 Esp8266Base 内置 WiFi 配网页，可查看并保存 STA 凭证；无已保存凭证时设备会进入 AP 配网模式。

`/ota` 是 Esp8266Base 内置 OTA 上传页，当前固件已通过 `ESP8266BASE_USE_OTA=1` 启用。GET 页面和 POST 上传都使用同一组 Basic Auth；上传页显示进度，上传期间基础库会配合 Watchdog 暂停/恢复，成功后重启。

`/auth` 是 Esp8266Base 内置改密页，用于修改 Web Basic Auth 密码。

`/logs` 使用 Esp8266Base 内置日志页面，读取 `/logs/app.log` 及滚动日志文件。一次新启动会有启动分割和启动次数，NTP 同步后日志时间使用真实日期时间。

`/reboot` 是 Esp8266Base 内置工具页，提供清空文件日志和重启设备操作；`/health` 是无需认证的 JSON 健康检查入口。

## HTTP API

所有 API 由 Esp8266Base Web 鉴权保护。

| 路径 | 方法 | 作用 |
|------|------|------|
| `/api/status` | GET | 获取风扇状态、网络状态、时钟和运行信息 |
| `/api/speed` | GET / POST | 读取或设置速度，POST 参数 `speed=0..100` |
| `/api/timer` | GET / POST | 读取或设置定时，POST 参数 `seconds=0..356400` |
| `/api/stop` | POST | 停止风扇并清除定时 |
| `/api/config` | GET / POST | 读取或保存配置参数 |
| `/api/ir/learn` | POST | 启动红外学习，POST 参数 `key_index=0..7`；清除红外码使用 `key_index=0..7&clear=1` |

由于 Esp8266Base 当前配置的应用 API 配额为 6 个，本项目只注册以上 6 个 API。红外学习状态由学习请求结果、`/api/status` 中的 `ir_learn_seq`、`ir_last_*`、`ir_reject_seq` 和 `ir_duplicate_key` 观察，不单独占用一个公开 API；学习序号变化表示本次学习完成，拒绝序号或重复键位表示本次按键已绑定到其他命令。

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

默认固件全局日志使用 DEBUG 等级，便于串口现场调试；文件日志使用 WARN 等级，只持久化 WARN 及以上级别事件，降低 Flash 写入量。配置读取审计由 Esp8266Base 的 `ESP8266BASE_CFG_READ_AUDIT_LEVEL` 单独控制；不定义该宏时沿用基础库默认行为。配置审计不做脱敏，便于个人项目调试观察。

## 配置项

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `fan_min_speed` | `10` | 最低有效速度，1-9% 会提升到此值 |
| `fan_sleep_wait` | `60` | 风扇停止后进入 Modem Sleep 的等待秒数 |
| `fan_soft_start` | `1000` | 软启动时间，毫秒 |
| `fan_soft_stop` | `1000` | 软停止时间，毫秒 |
| `fan_block_detect` | `1500` | 无 RPM 持续多久判定堵转，毫秒 |
| `fan_led_flash_ms` | `200` | 有效操作反馈闪烁时长，范围 0-2000ms，0 表示不闪 |
| `fan_auto_restore` | `true` | 上电后是否恢复上次速度和定时 |

运行状态还会持久化上次速度、剩余定时、累计运行时长和红外学习码。Web 密码由 Esp8266Base 使用 `eb_web_pass` 管理。

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
-DESP8266BASE_LOG_FILE_LEVEL=2
-DESP8266BASE_CFG_READ_AUDIT_LEVEL=0
```

## 依赖

- Esp8266Base：`https://github.com/tangyangguang/Esp8266Base`
- IRremoteESP8266：`crankyoldgit/IRremoteESP8266@^2.8.6`

`platformio.ini` 当前使用 `symlink://../Esp8266Base` 便于本机联调基础库，并使用 Esp8266Base 提供的 4 MB Flash / 2 MB 文件系统 linker script。外部 clone 或 CI 环境需要先准备同级 `Esp8266Base` 目录，或把 `lib_deps` 改为 Esp8266Base 仓库 URL 并同步处理 linker script 路径。

## 本地工具文件

`.gitignore` 会忽略 `CLAUDE.md`、`opencode.json`、`.codex-worklog.md` 和 `evaluations/` 等个人开发过程文件。这些文件不属于固件交付物；如果需要共享协作工具配置，请先把权限配置改为人工确认模式，避免把本机宽松执行策略带入团队仓库。

## 目录

```text
src/
  main.cpp                 引脚、模块实例和 Esp8266Base 初始化
  fan/                     风扇驱动、按键、LED、红外和控制状态机
  web/                     风扇专属 Web 页面和 API
docs/                      需求、硬件、软件架构和代码设计文档
test/                      native 单元测试和 Esp8266Base 测试替身
```
