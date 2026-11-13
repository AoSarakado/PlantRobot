# intelligent_plant 开发约定

1. 优先复用 ESP-IDF、乐鑫组件管理器和成熟开源组件；引入依赖前核对目标芯片、IDF 版本、许可与资源占用。
2. 分析故障先追踪实际数据流、调用顺序和硬件边界，再改代码。不要用兜底值掩盖初始化、采样或任务创建错误。
3. `main/main.c` 只放启动编排、I2C 总线建立和串口调试入口。业务流程放在 `main/soft_src`，接口放在 `include/soft_inc`。
4. `main/out_src` 与 `include/out_inc` 是原有硬件模块。只有修复驱动接口或并发问题时才修改，并说明原因。
5. 传感器任务是共享环境状态的唯一写入者。其他任务通过 `environment_snapshot()` 获取快照；不要直接读写状态内部变量。持锁期间不做 I2C、ADC、显示或 TTS 操作。
6. GY-MAX4466 模拟麦克风在 GPIO2，光敏在 GPIO1，两者共用 `analog_capture` 的 ADC1 连续采样。不要再建立 ADC1 oneshot 或 I2S 麦克风驱动。语音播报期间暂停 VAD，避免扬声器声音触发回应。
7. 修改后执行 `idf.py build`。涉及麦克风、TTS、传感器或显示行为时，编译通过仍需上板验证；未验证就明确标注。
8. 新增 `.c` 文件时同步更新 `main/CMakeLists.txt` 的 `SRCS` 列表；ESP-IDF 的组件依赖检查阶段不支持此处使用 `GLOB CONFIGURE_DEPENDS`。
9. 植物参考阈值必须在 README 记录原始来源、单位换算与适用范围。未知品种只报告参考画像的覆盖情况，不推断品种；未经校准的 ADC 百分比不能当照度。
