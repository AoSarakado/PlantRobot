# intelligent_plant —— 开发流程

ESP32-S3（N16R8：16MB flash / 8MB OCT PSRAM）植物监测 + I2S 语音。
**历史实测**：编译 → 烧写 → 串口日志（2026-11-06）。新增共享状态与自动语音交互已编译，尚未重新上板验证。

---

## 一、目录长这样，各管各的

```
D:\Embedded\projects\intelligent_plant\
  CMakeLists.txt            工程壳，只有三行
  sdkconfig.defaults        板子特性 + 自定义分区表（改配置改这里）
  partitions.csv            分区表真源
  build.bat                 在已激活 ESP-IDF 的终端中执行 build
  main\                     ← 你的 src 原样搬过来的，就是 main 组件
    CMakeLists.txt          显式列出源文件，新增模块时同步更新
    idf_component.yml       组件管理器依赖（esp32-camera / esp-sr）
    main.c
    out_src\                原有 8 个模块（oled/aht20/light/audio/mic/cam/mg90s/front）
    soft_src\               新增业务模块：共享环境状态、语音交互、TTS
  include\
    out_inc\                原有头文件
    soft_inc\               新增业务模块接口
  managed_components\       组件管理器下载 esp-sr（包含 TTS 库与声音集）等依赖，别手改
```

规矩很简单：**新模块只往 `soft_src` / `soft_inc` 里放**，并在 `main/CMakeLists.txt` 登记源文件；
`out_src` 是原有代码，别在里面加东西，方便对照原版。

---

## 二、一次完整的循环

```bat
D:\Embedded\idf-env.bat                  :: 每个 cmd 窗口激活一次
cd /d D:\Embedded\projects\intelligent_plant
idf.py set-target esp32s3                :: 每个新工程只做一次
idf.py build                             :: 改完代码就跑这个
idf.py -p COM7 flash monitor             :: 烧 + 看日志，退出 Ctrl+]
```

只想编译不想烧：`idf.py build`，或者双击 `build.bat`。
只想烧不重编：`idf.py -p COM7 flash`。

产物与判定点：

```
build\intelligent_plant.bin      当前 394,848 B（2M 分区用掉 19%）
build\flash_args                 四段地址都在这，voice_data 固定在 0x210000
```

---

## 三、分区表：改之前先算

```
# Name,       Type,  SubType,  Offset,     Size
nvs,          data,  nvs,      0x9000,     0x6000      24K
phy_init,     data,  phy,      0xf000,     0x1000       4K
factory,      app,   factory,  0x10000,    0x200000     2M
voice_data,   data,  fat,      0x210000,   0x300000     3M
```

三条规矩：

```
offset 必须 4KB 对齐
相邻分区不能重叠：前一个 end ≤ 后一个 offset
nvs 必须留在 0x9000 —— 挪了已存的配置全丢
```

声音集尺寸是硬约束：xiaole 2.80MB、xiaoxin 3.64MB。
所以 4MB flash 的板子放不下，这是物理死局，不是配置问题。

改完分区表**第一次要整片擦一次**，否则旧的分区头会留在 flash 里：

```bat
idf.py -p COM7 erase-flash
idf.py -p COM7 flash
```

---

## 四、sdkconfig 就三项关键

```
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y        不写会按 2MB 编，分区表用不上
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
```

PSRAM 那几项（`SPIRAM` / `MODE_OCT` / `SPEED_80M`）摄像头要用，也别删。
改完 `sdkconfig.defaults` 要删掉现成的 `sdkconfig` 再 build 才生效。

---

## 五、挂 TTS：两行代码

`main.c` 的 `app_main` 里加：

```c
#include "tts_module.h"
#include "audio_module.h"

tts_module_init("voice_data", audio_play_frame, audio_play_end);
tts_say("温度二十五点三度", 3);      // 非阻塞，立刻返回
```

`uart_task` 里加个命令（注意 BUF_LEN 要放大到 192，中文 3 字节/字）：

```c
else if (!strncmp(buf, "say ", 4)) {
    if (!tts_say(buf + 4, 3)) printf("say: queue full\r\n");
}
```

三条红线：

```
sink 内部自己会拿锁      别在 tts_module 外面再包 mutex（FreeRTOS mutex 非递归，包了死锁）
采样率 16k 写死          改它只会变调
中断/回调里别调 tts_say  它会 malloc
```

**验证 TTS 真的链进去了**（重要）：没被调用时，链接器会把整个 .o 丢掉，
所以"编译过"不等于"链上了"。

```bat
set NM=D:\Embedded\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin\nm.exe
%NM% build\intelligent_plant.elf | findstr tts_module_init
```

当前 `app_main` 已调用 `tts_module_init`，链接后的 ELF 应能搜到该符号。

---

## 六、首次上电的基线日志（2026-11-06 实测）

```
OLED ok
AHT20 ok
light: ok
audio: ok
servo: ok
mic_init: rate=16000 frames=480 ...
==== ready. cmds: tone lite temp scan mic micr micp rec play cam serv sv0~180 ====
s3 ll_cam: DMA Channel=1
cam_hal: cam init ok
camera: Detected OV2640 camera
cam_hal: Allocating 15360 Byte frame buffer in PSRAM
cam: ok
```

八件外设全过。以后哪一项变成 `FAIL`，就是那次改动碰到的，从这条基线往下对比最快。

---

## 七、踩过的坑（按恶心程度排）

```
1  PYTHONPATH 污染
   在有 hermes / 别的 venv 的终端里跑 idf.py，会报
   "The following Python requirements are not satisfied: click<8.2 ..." 然后
   "Activation script failed"。不是 IDF 坏了，是外来 site-packages 抢先。
   解法：env -u PYTHONPATH -u PYTHONHOME cmd /c build.bat

2  build 目录变残
   报 "doesn't seem to be a CMake build directory. Refusing to..."。
   直接删掉 build 重来；注意 shell 的 cwd 不能停在里面，否则删不掉。

3  串口被占 / 掉线
   "could not open port"=掉线（重插）；
   "拒绝访问"=真被别的程序独占（关串口助手）；
   PermissionError(13)+设备没有发挥作用=USB 链路假死（换口或 pnputil /restart-device）。

4  I2S 念完一直响
   没设 chan_cfg.auto_clear = true，DMA 排空后通道还开着会重复送残留数据。
   audio_module.c 里本来就设了，自己另开通道时别忘。

5  TTS 来源
   当前从组件管理器的 espressif/esp-sr 获取 TTS 库与声音集。
   不再在仓库内复制二进制资产。
```

---

## 八、日常备忘

```bat
:: 只看串口（不烧）
idf.py -p COM7 monitor

:: 看当前分区表
idf.py partition-table

:: 看各组件占多少 flash
idf.py size-components

:: 查芯片型号 / flash 大小
esptool --port COM7 flash_id
```
