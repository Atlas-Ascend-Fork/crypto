# core — 运行时与平台层

所有算法族共用的控制面。

职责：

- Handle / Stream / workspace / plan 缓存。
- SoC 与实现路径的注册和分发。
- 密钥对象管理：导入、device 常驻缓存、usage 限制、销毁清零。
- 与 AI 负载共存的资源配额：stream 优先级、core 预算。
- 错误码、日志、公共参数校验、内存清零。

约束：不依赖任何具体算法。
