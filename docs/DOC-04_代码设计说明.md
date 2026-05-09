# DOC-04 代码设计说明

| 字段 | 内容 |
|------|------|
| 文档编号 | DOC-04 |
| 项目名称 | 壁炉烟囱正压送风控制器 |
| 版本 | v1.2 |
| 日期 | 2026-04-30 |
| 状态 | 已确认 |

---

## 1. 核心数据结构

### 1.1 系统配置（通过 Esp8266BaseConfig 存储）

| 键名 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `fan_min_speed` | int32_t | 10 | 最低有效转速，范围 0-50，单位% |
| `fan_soft_start` | int32_t | 1000 | 软启动渐变时间，单位毫秒，范围 0-10000 |
| `fan_soft_stop` | int32_t | 1000 | 软停止渐变时间，单位毫秒，范围 0-10000 |
| `fan_block_detect` | int32_t | 1500 | 堵转检测时间，单位 ms，范围 100-5000 |
| `fan_sleep_wait` | int32_t | 60 | 风扇停止后到进入休眠的等待时间，单位秒，范围 0-3600 |
| `fan_led_flash_ms` | int32_t | 200 | 有效操作反馈闪烁时长，单位 ms，范围 0-2000；0 表示关闭操作反馈 |
| `fan_auto_restore` | bool | true | 断电后是否自动恢复运行状态（false=上电停止） |
| `fan_last_speed` | int32_t | 0 | 上次目标转速，断电恢复用 |
| `fan_last_timer` | int32_t | 0 | 上次剩余定时时间，断电恢复用 |
| `fan_run_duration` | int32_t | 0 | 累计总运行时长，单位秒，掉电不丢失 |
| `fan_ir_key_0` ~ `fan_ir_key_5` | string | 空 | 6 个红外按键的 `protocol:hex_code` 编码 |

### 1.2 运行时状态（内存中）

| 字段 | 类型 | 说明 |
|------|------|------|
| `current_gear` | uint8_t | 当前档位，范围 0-4 |
| `current_speed` | uint8_t | 当前实际输出转速，范围 0-100 |
| `target_speed` | uint8_t | 目标转速，渐变过程中使用 |
| `timer_remaining` | uint32_t | 定时剩余时间，单位秒，0=无定时 |
| `run_duration` | uint32_t | 总累计运行时长，单位秒，不清零 |
| `uptime` | uint32_t | 本次启动后连续运行时长，单位秒 |
| `is_blocked` | bool | 是否处于堵转保护状态 |
| `wifi_connected` | bool | WiFi 是否连接成功 |
| `ntp_synced` | bool | NTP 网络时间是否同步成功 |
| `ip_address` | char[16] | 设备 IP 地址 |
| `mac_address` | char[18] | WiFi MAC 地址，格式 xx:xx:xx:xx:xx:xx |
| `wifi_rssi` | int8_t | WiFi 信号强度，单位 dBm |
| `error_code` | uint16_t | 错误码，0=无错误，1=堵转，2=配置损坏 |

### 1.3 档位设计

| 档位 | 速度 | LED 亮度 | 说明 |
|------|------|---------|------|
| 0 档 | 0% | 熄灭 | 停止 |
| 1 档 | 25% | 25% PWM | 低速 |
| 2 档 | 50% | 50% PWM | 中速 |
| 3 档 | 75% | 75% PWM | 高速 |
| 4 档 | 100% | 100% PWM | 全速 |

---

## 2. 行为规则

### 2.1 正常运行规则

1. **转速控制**：0-100% 无级调节；加速时设置值<最低有效转速自动提升，减速时设置值≤最低有效转速直接降到 0；启动时按软启动时间平滑渐变，停止时按软停止时间平滑渐变
2. **双按键控制**：加速键短按档位 +1（最高到 4 档，不循环），减速键短按档位 -1（最低到 0 档，不循环），同时长按 >5s 恢复出厂设置
3. **红外遥控控制**：支持 NEC/SONY/RC5/RC6/SAMSUNG 等常见协议；通过 Web 页面触发学习模式，按下遥控器对应按键自动记录协议类型和编码值；加速/减速按 0/25/50/75/100 档位调整
4. **Web 控制**：支持状态查询、转速调节、定时设置、参数配置、固件升级、日志页面，通过 `esp-fan.local` 访问
5. **定时功能**：倒计时结束自动停止，定时过程中可随时取消或修改，新设置直接覆盖旧定时；页面提供 30min/1h/2h 预设，自定义最大 99 小时
6. **堵转检测**：输出≥最低有效转速且连续检测时间无转速反馈时，立即切断输出并报警；保护触发后任意控制源发送启动指令可触发恢复尝试（1.5s 内有转速则恢复）；重启自动清除
7. **低功耗休眠**：风扇停止且无操作超时后进入 Modem Sleep，保持 WiFi 连接和 Web 服务可访问，响应<1000ms
8. **断电恢复**：`auto_restore=true` 时恢复到断电前状态（转速 + 定时剩余时间），`auto_restore=false` 时上电停止
9. **运行时长统计**：持续累计总运行时长和本次启动运行时长，不清零，掉电不丢失
10. **WiFi 重连**：连接失败后默认 30 秒重试，连续 5 次失败后延长到 5 分钟

### 2.2 优先级规则

1. **停止指令优先级最高**：任何状态下收到停止指令，立即执行停止，取消所有定时任务
2. **故障保护优先级高**：堵转保护触发后，任意控制源均可发送启动指令触发恢复尝试
3. **三种控制方式平等**：本地/红外/Web 指令优先级相同，最后一次收到的指令生效
4. **手动操作覆盖定时**：定时运行过程中收到手动调速/停止指令，自动取消当前定时任务

### 2.3 异常处理规则

1. **配置读取失败**：自动加载默认配置，记录告警日志，不影响正常运行
2. **WiFi 连接失败**：前 5 次每隔 30 秒重试，之后每隔 5 分钟重试，不影响本地/红外控制
3. **堵转保护触发**：立即切断风扇输出，记录故障日志；任意控制源发送启动指令可触发恢复尝试
4. **红外接收错误**：误码或无效编码直接丢弃，不执行任何操作
5. **红外学习超时**：学习模式 10 秒超时自动退出，不保存未学习的按键

---

## 3. 模块接口定义

### 3.1 FanDriver（HAL 层）

```cpp
enum FanState {
    FAN_STATE_IDLE,
    FAN_STATE_SOFT_START,
    FAN_STATE_RUNNING,
    FAN_STATE_SOFT_STOP,
    FAN_STATE_BLOCKED
};

class FanDriver {
public:
    FanDriver(uint8_t pwm_pin, uint8_t tach_pin);
    bool begin();
    void tick();  // 每帧调用，处理软启动/软停止渐变和堵转检测

    bool setSpeed(uint8_t speed);
    uint8_t getSpeed() const;
    uint16_t getRpm() const;
    FanState getState() const;

    void setSoftStartTime(uint16_t ms);
    void setSoftStopTime(uint16_t ms);
    void setBlockDetectTime(uint16_t ms);

    bool isBlocked() const;
    void resetBlock();
};
```

### 3.2 ButtonDriver（HAL 层）

```cpp
enum ButtonEvent {
    BTN_NONE,
    BTN_ACCEL_SHORT,    // 加速键短按
    BTN_DECEL_SHORT,    // 减速键短按
    BTN_BOTH_LONG       // 两键同时长按 >5s
};

class ButtonDriver {
public:
    ButtonDriver(uint8_t accel_pin, uint8_t decel_pin);
    bool begin();
    ButtonEvent getEvent();  // 每帧调用
};
```

### 3.3 LedIndicator（HAL 层）

```cpp
enum LedMode {
    LED_OFF,           // 熄灭
    LED_ON,            // 常亮
    LED_SLOW_BLINK,    // 慢闪 1Hz
    LED_FAST_BLINK,    // 快闪 5Hz
    LED_SINGLE_FLASH   // 闪1下后恢复原状态
};

class LedIndicator {
public:
    LedIndicator(uint8_t pin, bool active_low = true);
    bool begin();
    void tick();  // 每帧调用

    void setGear(uint8_t gear);  // 0-4 档，自动设置 PWM 亮度
    void setOverride(LedMode mode);  // WiFi/故障等覆盖模式
    void flashOnce();  // 操作反馈闪1下
};
```

实现约定：

- 档位亮度使用固定表 `0/64/128/192/255`，避免 4 档亮度在 `uint8_t` 中溢出。
- `LedIndicator` 只维护当前档位、基础模式、闪烁窗口、反馈时长和输出状态；`flashOnce()` 是临时覆盖，结束后恢复当时最新的基础状态。
- 操作反馈时长默认 200ms，通过 `fan_led_flash_ms` 配置为 0-2000ms；0 表示关闭操作反馈闪烁。
- `FanController` 每个 tick 在业务状态机处理完成后统一调用 LED 决策，再执行 `_led.tick()`。
- LED 优先级固定为：`SYS_ERROR` 或堵转时快闪 > WiFi 未连接慢闪 > 当前档位亮度/熄灭。
- 所有有效用户操作闪 1 下：Web 调速/停止/定时/配置保存、按键有效加减档、IR 有效操作、红外学习成功。非法 API 参数、边界重复加减档、未知红外码、学习超时不闪。
- 故障恢复期间保持快闪；`setSpeed()` 在故障态只发起恢复尝试，恢复成功后回到普通 LED 规则，恢复失败继续快闪；`stop()` 清除故障后回到 WiFi 慢闪或 0 档熄灭。

### 3.4 IRReceiverDriver（HAL 层）

```cpp
enum IREvent {
    IR_EVENT_NONE,
    IR_EVENT_SPEED_UP,
    IR_EVENT_SPEED_DOWN,
    IR_EVENT_STOP,
    IR_EVENT_TIMER_30M,
    IR_EVENT_TIMER_1H,
    IR_EVENT_TIMER_2H
};

class IRReceiverDriver {
public:
    IRReceiverDriver(uint8_t recv_pin);
    bool begin();
    IREvent getEvent();  // 每帧调用

    // 红外学习
    bool startLearning(uint8_t key_index);  // 进入学习模式，key_index: 0-5
    bool isLearning() const;
    uint8_t getLearnedKeyIndex() const;

    // 配置
    void setKeyCode(uint8_t key_index, uint8_t protocol, uint64_t code);
    bool getKeyCode(uint8_t key_index, uint8_t* protocol, uint64_t* code) const;
};
```

### 3.5 FanController（Business 层）

```cpp
enum SystemState {
    SYS_INIT,
    SYS_IDLE,
    SYS_RUNNING,
    SYS_SLEEP,
    SYS_ERROR
};

class FanController {
public:
    FanController(FanDriver& fan, ButtonDriver& btn, LedIndicator& led, IRReceiverDriver& ir);
    bool begin();
    void tick();  // 每帧调用

    SystemState getState() const;
    uint8_t getCurrentGear() const;
    uint8_t getCurrentSpeed() const;
    uint32_t getTimerRemaining() const;
    uint32_t getTotalRunDuration() const;
    bool isBlocked() const;

    // 外部指令接口，供 Web/其他模块调用
    bool setSpeed(uint8_t speed);
    bool setTimer(uint32_t seconds);
    bool stop();

private:
    void _handleInit();
    void _handleIdle();
    void _handleRunning();
    void _handleSleep();
    void _handleError();
    void _processButtonEvents();
    void _processIREvents();
    void _processTimer();
    void _processSleep();
    void _saveRuntimeState();
};
```

### 3.6 FanWeb（Application 层）

```cpp
class FanWeb {
public:
    FanWeb(FanController& controller, IRReceiverDriver& ir);
    // 页面处理函数
    static void handleStatusPage();
    static void handleConfigPage();

    // API 处理函数
    static void handleApiStatus();
    static void handleApiSpeed();
    static void handleApiTimer();
    static void handleApiStop();
    static void handleApiConfig();
    static void handleApiIrLearn();

private:
    static FanController* _controller;
    static IRReceiverDriver* _ir;
};
```

---

## 4. Web 页面设计

### 4.1 访问方式

- **mDNS**：`http://esp-fan.local/`
- **IP**：`http://[设备 IP 地址]/`
- **鉴权**：HTTP Basic Auth，用户名 `admin`，默认密码 `admin123`

### 4.2 页面清单

| 路径 | 功能 | 内容 |
|------|------|------|
| `/fan` | 状态主页 | 当前转速、目标转速、定时剩余时间、累计运行时长、WiFi 状态/信号强度/IP、当前时间、故障状态 |
| `/config` | 参数配置页 | 最低有效转速、软启动/软停止时间、堵转检测时间、休眠等待时间、访问密码、红外学习、上电恢复策略 |
| `/esp8266base` | 系统首页 | Esp8266Base 内置 Network、Device、Time 状态页，显示 OTA free 等基础状态 |
| `/wifi` | WiFi 配网页 | Esp8266Base 内置 STA/AP 配网入口 |
| `/ota` | OTA 升级页 | Esp8266Base 内置 Web OTA 页面和上传处理，已通过 `ESP8266BASE_USE_OTA=1` 启用 |
| `/logs` | 运行日志页 | Esp8266Base 内置文件日志页面 |
| `/auth` | 密码页 | Esp8266Base 内置 Web 密码修改页面 |
| `/reboot` | 工具页 | Esp8266Base 内置清空日志和重启入口 |
| `/health` | 健康检查 | Esp8266Base 内置无需认证 JSON 健康信息 |

### 4.3 状态主页布局

```
┌─────────────────────────────────────┐
│  壁炉烟囱正压送风控制器              │
├─────────────────────────────────────┤
│  当前档位：2 档                      │
│  当前转速：50%                       │
│  运行状态：运行中                    │
│  定时剩余：30 分钟                   │
│  累计运行时长：12 小时 35 分         │
├─────────────────────────────────────┤
│  WiFi：已连接 | 信号：-65 dBm       │
│  IP：192.168.1.100                  │
│  NTP：已同步 | 当前时间：2026-04-30  │
├─────────────────────────────────────┤
│  [调速滑块] [停止] [定时设置]        │
└─────────────────────────────────────┘
```

---

## 5. Web Service API

所有 API 采用 HTTP Basic Auth 鉴权，用户名 `admin`，默认密码 `admin123`。返回 JSON 格式，HTTP 200 表示操作成功，参数非法返回 HTTP 400。

| 方法 | 路径 | 功能 | 请求示例 | 返回示例 |
|------|------|------|----------|----------|
| GET | `/api/status` | 获取设备运行状态 | - | `{"ok":true,"data":{"state":"Running","speed":50,"target_speed":50,"timer_remaining":1800,"run_duration":3600,"blocked":false,"ip":"192.168.1.100","rssi":-65,"clock":"2026-05-09 12:00:00","ir_learning":false,"ir_key":0,"ir_remaining":0,"ir_learn_seq":1,"ir_last_protocol":1,"ir_last_code":"0x0000E01F"}}` |
| POST | `/api/speed` | 设置风扇转速 | `speed=70` | `{"ok":true,"speed":70,"target_speed":70}` |
| POST | `/api/timer` | 设置定时关机 | `seconds=3600` | `{"ok":true,"timer_remaining":3600}` |
| POST | `/api/stop` | 立即停止风扇 | - | `{"ok":true}` |
| GET | `/api/config` | 获取配置参数 | - | `{"ok":true,"data":{"min_effective_speed":10,"soft_start":1000,"soft_stop":1000,"block_detect":1500,"sleep_wait":60,"led_flash_ms":200,"auto_restore":true}}` |
| POST | `/api/config` | 修改配置参数 | `min_speed=15&soft_start=500` | `{"ok":true,"changed":2,"flushed":true,"data":{"min_effective_speed":15,"soft_start":500,"soft_stop":1000,"block_detect":1500,"sleep_wait":60,"led_flash_ms":200,"auto_restore":true}}` |
| POST | `/api/ir/learn` | 开始红外学习 | `key_index=0` | `{"ok":true,"learning":true,"timeout":10}` |

---

## 6. 持久化存储设计

### 6.1 存储方案

使用 Esp8266Base 的配置模块保存 KV 数据。

### 6.2 命名空间规划

| 命名空间 | 用途 | 键数量 |
|----------|------|--------|
| `fan` | 风扇配置和运行时状态 | 20 个 |
| Esp8266Base 内置键 | WiFi 凭证、Web 密码等基础库配置 | 由基础库管理 |

### 6.3 版本迁移策略

- 启动时读取配置，与当前固件期望版本比较
- 版本一致：正常加载所有配置
- 版本不一致：加载所有默认配置，记录告警日志，保留用户 WiFi 配置
- 读取失败（首次烧录/Flash 损坏）：写入所有默认配置，进入 AP 配网模式
- 出厂重置时：清除 `fan` 命名空间所有配置，恢复默认值

### 6.4 日志存储

使用 Esp8266Base 的文件日志能力，日志写入 `/logs/app.log` 文件：
- 每条日志格式：`[时间戳] [级别] [模块] 消息内容`
- NTP 同步后优先使用真实日期时间
- 全局日志等级为 DEBUG，文件日志等级为 WARN
- 单文件 16KB，保留 4 个滚动文件

---

## 7. 初始化流程

`Esp8266Base::begin()` 自动按依赖顺序初始化所有启用模块：

1. **核心模块**：Log、Sleep、Config
2. **连接与保护**：WiFi、Watchdog
3. **Web 与升级**：Web 先注册内置路由，OTA 随后注册 `POST /ota`
4. **循环处理**：WiFi、Config deferred flush、NTP、mDNS、Web、Watchdog 等由 `Esp8266Base::handle()` 持续推进

**项目代码只需调用**：
```cpp
Esp8266Base::setFirmwareInfo("ESP12F_Fan_4P", "0.1.0");
Esp8266Base::setHostname("esp-fan");
Esp8266Base::begin();
```

---

## 8. 引脚配置

| GPIO | 功能 | 说明 |
|------|------|------|
| GPIO0 | BOOT 键 | 长按 1s 清除 WiFi 凭证 |
| GPIO2 | 板载 LED | ESP-12E 模块内置，低电平亮，支持 PWM 调光 |
| GPIO4 | 减速键 | 内部上拉，按下为低，仅短按 |
| GPIO5 | PWM_OUT | 风扇 PWM 输出，频率 25KHz |
| GPIO12 | FAN_TACH | 风扇转速反馈输入 |
| GPIO13 | IR_RECV | 1838 红外接收头 |
| GPIO14 | 加速键 | 内部上拉，按下为低，仅短按 |
| GPIO16 | 保留 | 深度睡眠唤醒预留（需连接 RST） |
