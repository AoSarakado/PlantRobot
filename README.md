# intelligent_plant

ESP32-S3 N16R8 植物环境监测工程，基于 ESP-IDF 5.5。读取 AHT20 温湿度与光敏 ADC，在 OLED 和串口显示。目前麦克风检测到一段讲话结束后，由 ESP-TTS 播报当前环境；目标语音链路是接收人声、调用 LLM 对话并合成陪伴式回复，当前环境播报仅是临时阶段。LED 表情功能待确定屏幕硬件后接入。

## 待实现的交互目标

- 语音：采集人声 → 语音转文字 → LLM 结合环境与植物状态生成陪伴式回复 → TTS 播放。现有 ESP-SR VAD 只判断是否有人声；`stt` 命令已可将录音发送至本机 ASR 服务，自动对话链路尚未接入。
- LLM 采用二选一提供者：自建服务端部署模型，或调用云端模型服务。ESP32-S3 不运行 LLM。`include/soft_inc/llm_provider.h` 已预留统一的请求/回复接口；语音转文字与 Wi-Fi 上传已有独立调试路径，两种 LLM 提供者和完整对话链路尚未实现。
- 植物状态：先基于可信的温度、空气湿度和光照数据判断环境状况；缺少土壤湿度时，不推断植物是否缺水。

## 结构

| 路径 | 职责 |
| --- | --- |
| `main/main.c` | 外设初始化、FreeRTOS 任务启动、串口调试命令 |
| `main/out_src`、`include/out_inc` | 原有 OLED、传感器、音频、麦克风、摄像头和舵机驱动 |
| `main/soft_src/environment_state.c` | 每 2 秒采样，维护带有效标记的共享环境快照 |
| `main/soft_src/plant_assessment.c` | 常见家养植物参考画像与温度判断 |
| `main/soft_src/voice_interaction.c` | ESP-SR VAD 检测讲话结束，生成环境播报文本 |
| `main/soft_src/asr_client.c` | Wi-Fi 录音上传与转写结果读取；串口 `stt` 命令 |
| `server/asr_server.py` | 本机 faster-whisper 转写服务，接收 16 kHz 单声道 PCM |
| `main/soft_src/tts_module.c` | ESP-TTS 队列和音频输出回调 |
| `main/idf_component.yml` | esp32-camera 与 esp-sr 依赖 |
| `partitions.csv` | 应用及语音数据分区；声音集来自官方 `espressif/esp-sr` 组件 |

共享状态由传感器任务写入；OLED、串口与语音任务复制快照后使用。GY-MAX4466 的模拟 OUT 接 GPIO2，光敏接 GPIO1；两者由 `analog_capture` 统一使用 ADC1 连续采样。录音、回放、实时监听和自动语音交互不能同时占用麦克风流。

## 构建

在 ESP-IDF 5.5 命令环境中首次执行 `idf.py set-target esp32s3`，以后执行 `idf.py build`。烧录时使用 `idf.py -p COM端口 flash monitor`。组件管理器会按 `main/idf_component.yml` 下载乐鑫 ESP-SR（包含 TTS 库和声音集）与摄像头组件；本仓库不重复保存第三方二进制。ESP-SR 的许可见[上游 LICENSE](https://github.com/espressif/esp-sr/blob/master/LICENSE)。

`sdkconfig` 包含本机 Wi-Fi 凭据，已被 Git 忽略；不要提交它。`sdkconfig.defaults` 只保存非私密的板级默认设置。`build/`、`managed_components/`、Python 虚拟环境和诊断录音同样不入库。

串口命令包括 `lite`、`temp`、`state`、`scan`、`mic`、`micr`、`micp`、`rec`、`play`、`stt`、`say 文本`、`tone`、`cam`、`serv` 和 `sv角度`。硬件引脚、分区与详细操作见 [开发流程](README-开发流程.md)。

## 本机语音转文字

本机服务端复用 [faster-whisper](https://github.com/SYSTRAN/faster-whisper)。`stt` 命令录制 3 秒、上传原始 16 kHz/16-bit/单声道 PCM，服务端返回 UTF-8 文字，设备在串口打印 `stt: ...`。这条链路只做转写，还未调用 LLM；自动 VAD 环境播报暂时保留。板上采样和网络传输已验证，但模拟麦克风存在强周期噪声，转写质量尚未过关。

1. 在工程目录执行 `python -m venv server/.venv`，然后 `server/.venv/Scripts/python.exe -m pip install -r server/requirements.txt`。
2. 执行 `server/.venv/Scripts/python.exe server/asr_server.py`。默认 CPU `small` 模型；首次启动会从 Hugging Face 下载模型，下载完成并出现 `ASR listening` 后才可请求。可用 `PLANT_ASR_MODEL` 环境变量选其他 faster-whisper 模型或本地已下载的 CTranslate2 模型目录。
3. 执行 `idf.py menuconfig`，在 **Plant speech transcription** 中填写 Wi-Fi SSID、密码和 `http://本机局域网IP:8765/transcribe`。本机地址可能随网络变化；设备与本机须可互访。配置会写入 `sdkconfig`，勿公开含密码的配置文件。
4. `idf.py -p COM端口 flash monitor`，串口输入 `stt` 并在录音时说话。若本机防火墙拦截 8765 端口，需要允许该服务在当前局域网访问。

服务端协议固定为 `POST /transcribe`，请求体 `application/octet-stream`，响应体 `text/plain; charset=utf-8`。失败返回非 200 状态，设备输出错误码。当前没有网络凭据时，`stt` 明确报配置错误，不会假装已转写。

服务端会打印每段 Whisper 的 `no_speech_prob` 用于诊断。2026-11-13 的诊断 WAV 在无人说话时出现稳定约 129 Hz 的周期信号，RMS 约 0.269；说话与静音都可能输出同一句字幕。服务端可复用 [noisereduce](https://github.com/timsainb/noisereduce) 的静态噪声门：设置 `PLANT_ASR_NOISE_WAV=D:\Embedded\projects\intelligent_plant\server\diagnostic_silence.wav` 后重启服务，明确加载这份静音录音作噪声参考。未设置时服务端明确显示 `noise reference=disabled`。离线对照中静音 RMS 从 0.269 降到 0.043，最新录音从 0.213 降到 0.073，但尚未证明真实讲话能被准确转写；仍需定位噪声的硬件来源。

验证记录（2026-11-13）：固件 `idf.py build`、本机 HTTP 协议测试通过；ESP32-S3 已通过 Wi-Fi 上传 PCM，本机 faster-whisper 已完成推理。当前录音中约 129 Hz 的周期噪声占主导，静音和讲话都可能产生错误字幕。软件降噪已降低 RMS，但没有证明人声转写准确。

## Reference profiles

首版选用绿萝、虎尾兰和吊兰作为参考画像。未知品种时，`state` 命令报告三份资料中有几份判定温度低于下限、几份落在文献明确的适宜范围；不把多数票当成植物身份或健康诊断。

| 参考植物 | 已录入的温度资料 | 来源与解释 |
| --- | --- | --- |
| 绿萝 pothos | 15.5–26.5°C 参考室温 | [Penn State Extension](https://extension.psu.edu/pothos-as-a-houseplant) 建议 60–80°F；摄氏值取近似数。15.5°C 暂作参考下限，不等于死亡温度。 |
| 虎尾兰 snake plant | 15–24°C 生长较佳；保持高于 10°C | [RHS](https://www.rhs.org.uk/plants/sansevieria)。 |
| 吊兰 spider plant | 理想情况下高于 10°C；没有录入适宜上限 | [RHS](https://www.rhs.org.uk/plants/3761/chlorophytum-comosum/details)。没有上限时返回未知，不假设高温也适宜。 |

资料中的“低／中／亮间接光”是环境类别，现有光敏电阻 ADC 百分比未经照度校准，不能直接和这些类别比较。[明尼苏达大学 Extension](https://extension.umn.edu/garden-and-home/yard-and-garden/gardening-in-minnesota/lighting-for-indoor-plants) 给出了不同室内光照条件下的植物类别。空气湿度适宜值因植物而异，当前不填没有可靠来源支持的统一数值阈值；[Penn State Extension](https://extension.psu.edu/humidity-and-houseplants) 也指出各类植物的最佳湿度不同且不是精确常数。缺少土壤湿度数据时不判断缺水。
