# firmware(M1:平台跑通)

M5Stack Tab5(ESP32-P4)上的 Brookesia chatbot 基线固件:小智语音助手 + 表情主屏 + 设置页 + Wi-Fi 配网。后续里程碑在它之上加 Launcher、音乐、导航。

- 工具链:**ESP-IDF v5.5.3**(原因见 [ADR-0004](../docs/adr/0004-IDF-5.5-与-Brookesia-0.7-基线.md))
- 框架:ESP-Brookesia **0.7.x**,`esp_lvgl_adapter 0.5.x`,LVGL 9.6
- 板级:`boards/m5stack_tab5`(基于 `esp_board_manager` 自带的 Tab5,改成 Brookesia 0.7 的配置)
- 来源与许可:见 [NOTICE](NOTICE)

## 构建

```bash
. ~/esp/esp-idf-v5.5.3/export.sh
cd firmware
idf.py gen-bmgr-config -c ./boards -b m5stack_tab5   # 生成 components/gen_bmgr_codes(已被 .gitignore)
idf.py set-target esp32p4
idf.py build
```

第一次 `set-target` 会下载约 100 个组件到 `managed_components/`,配置阶段需要几分钟;完整编译约 2800 个编译单元。

## 烧录与运行(需要接上 Tab5)

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

控制台走 USB Serial/JTAG(`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`),用 Tab5 的 USB-C 口。

## 当前状态与已知限制

| 项 | 状态 |
| --- | --- |
| 编译 | 见 [docs/07-M1-进展.md](../docs/07-M1-进展.md) |
| 屏幕 | 面板原生竖屏 720×1280;ILI9881C / ST7121 / ST7123 由板级代码在运行时识别(本机为 ST7121)。横屏旋转未做,见 docs/06 的 T17 |
| 麦克风 | 通道布局暂用 `RMNN`(参考通道 + 单麦),双麦布局待核对 ES7210 通道顺序(docs/06 的 T18) |
| 唤醒词 | 见 `sdkconfig.defaults.esp32p4` |
| 摄像头 | 板级配置里已去掉,P2 再加 |
| 实机验证 | 已烧录并稳定启动,见 [docs/07-M1-进展.md](../docs/07-M1-进展.md);语音对话与人工确认项未做 |

## 目录

```
firmware/
├── CMakeLists.txt
├── idf_ext.py                       Brookesia 示例的 idf.py 扩展(处理 gen-bmgr-config 的板级路径)
├── partitions_16m.csv               16MB 分区表(factory、model、storage、littlefs_data、anim_icon)
├── sdkconfig.defaults(.esp32p4)     工程默认配置
├── boards/m5stack_tab5/             Tab5 板级(YAML + setup_device.c + sdkconfig.defaults.board)
├── littlefs/xiaozhi/*.mp3           小智激活码语音
└── main/                            应用入口与模块(AI Agents、Display、Wi-Fi 配网、Profiler)
```
