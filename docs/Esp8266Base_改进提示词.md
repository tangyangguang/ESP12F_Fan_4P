# Esp8266Base 改进提示词

本文件记录本项目复核后确认属于 Esp8266Base 边界的改进项。本项目不在应用层绕开基础库能力，后续应在 `https://github.com/tangyangguang/Esp8266Base` 中设计和实现。

## 1. 配置批量提交 / 事务能力

背景：`ESP12F_Fan_4P` 的 `/api/config` 会一次保存多个 fan 配置项。当前只能逐个 `setInt/setBool/setStr`，再调用一次 `flush()`，断电时可能出现部分字段已进入内存或已被分批写入的中间状态。

目标：在 Esp8266Base 配置模块提供批量配置提交能力，让应用可以先完成全部校验，再以一个事务提交多个 KV 变更。

建议能力：
- 提供类似 `beginTransaction()` / `set*()` / `commit()` / `rollback()`，或 `commitBatch(entries)` 的接口。
- `commit()` 成功前不得破坏现有可读配置；失败时保留旧值并返回明确错误。
- 仍保留配置写审计日志，审计应能体现 batch id、key、old/new、commit 成败。
- 兼容当前单 key API；旧 API 可继续作为单 key batch 的语义实现。

为什么不在本项目绕开：应用层直接写文件会破坏 Esp8266Base 的审计、缓存、延迟 flush 和未来迁移能力，也会让所有项目重复实现一致性逻辑。

## 2. 按前缀清理配置

背景：`ESP12F_Fan_4P` 的双键长按 5 秒目前调用 `Esp8266BaseConfig::clearAll()` 做出厂重置，会清空 fan 配置、WiFi 凭证和 Web 密码。若后续希望实现“仅清除应用配置，保留 WiFi/Web 凭证”，基础库需要支持按命名空间或前缀清理。

目标：在 Esp8266Base 配置模块提供安全的按前缀清理能力，例如 `clearByPrefix("fan_")`。

建议能力：
- 支持列出、删除指定前缀的配置键，并通过一次 flush 原子落盘。
- 删除前后记录配置审计日志。
- 明确内置键前缀，例如 `eb_`，应用项使用自己的前缀，例如 `fan_`。
- 提供 dry-run 或返回删除数量，便于 Web 页面展示重置影响。

为什么不在本项目绕开：本项目不了解基础库内部配置索引、缓存和持久化细节；应用层手动枚举删除会遗漏未来新增键，也无法保证和基础库审计/flush 语义一致。
