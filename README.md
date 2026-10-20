# Tab5 单车智能车机(Tab5-Bike-Cockpit)

在 M5Stack Tab5(ESP32-P4)上做一台装在自行车车把上的智能车机:语音优先、触屏为辅,提供离线导航、音乐和轻量小游戏,联网走手机热点,外设走 BLE。

- 技术路线:ESP-Brookesia 0.7 服务层 + 小智 Agent + 自研 Launcher(路线 A),**ESP-IDF v5.5.3**(原因见 ADR-0004);离线地图用 `map_tiles` v2
- 目标用户:喜欢骑行、能接受刷固件的 DIY 玩家和创客
- 项目状态:立项完成(2026-10-16);M1 固件已烧录并稳定启动(2026-10-19),屏幕、触摸、扬声器已人工确认(2026-10-20,黑屏问题见 [docs/08](docs/08-显示黑屏排查.md)),**语音对话未做**,见 [docs/07-M1-进展.md](docs/07-M1-进展.md)

## 文档索引

| 文档 | 内容 |
| --- | --- |
| [docs/01-立项书.md](docs/01-立项书.md) | 背景、目标、范围、成功标准、角色、预算项 |
| [docs/02-需求规格.md](docs/02-需求规格.md) | 功能清单与优先级、各模块需求、MCP 工具、非功能指标 |
| [docs/03-技术方案.md](docs/03-技术方案.md) | 架构分层、模块设计、数据与存储、关键机制、构建与发布 |
| [docs/04-开发计划.md](docs/04-开发计划.md) | 里程碑、任务拆解、验收标准、排期草案 |
| [docs/05-风险与测试计划.md](docs/05-风险与测试计划.md) | 风险登记、待验证问题、测试策略与实车测试清单 |
| [docs/06-技术调研记录.md](docs/06-技术调研记录.md) | 资料调研结论、待验证项 T1–T21 的答案与来源 |
| [docs/07-M1-进展.md](docs/07-M1-进展.md) | M1 做了什么、没验证什么、接上 Tab5 后的验证清单 |
| [docs/08-显示黑屏排查.md](docs/08-显示黑屏排查.md) | Tab5 背光亮、屏幕全黑的排查记录与根因(LCD_RST 拉高方式) |
| [docs/adr/](docs/adr/) | 架构决策记录(0001 路线,0002 工具链与版本,0003 地图与路线,0004 IDF 5.5 + Brookesia 0.7) |

产品功能设计文档原稿(Claude 文档):https://claude.ai/artifact/Pbreoa3DCZFWzmkJoCpghU

## 目录结构

```
Tab5-Bike-Cockpit/
├── docs/            立项与设计文档
│   └── adr/         架构决策记录
├── firmware/        ESP-IDF 工程(P4 主固件),构建说明见 firmware/README.md
├── hardware/        车把支架、防水壳、接线图、BOM
├── tools/
│   └── map-export/  电脑端离线地图瓦片 / route.bin 导出工具
└── assets/          图标、封面、字体、UI 设计稿
```

## 硬件清单(首版)

- M5Stack Tab5(ESP32-P4,5 英寸 1280×720 触屏,32MB PSRAM,16MB Flash,C6 协处理器负责 Wi-Fi/BLE)
- M5Stack Unit GPS v1.1(U032-V11),接 Tab5 的 PORT.A(HY2.0-4P,G53 / G54)做 UART
- microSD(≥32GB,Class 10):地图与音乐
- NP-F550 电池或充电宝 USB-C 5V
- 车把支架(Tab5 侧面 1/4 英寸螺孔)、防水壳 / 防雨罩、防眩光膜

## 约定

- 文档语言:中文;代码标识符、提交信息用英文
- 文档中标注「待验证」的内容必须在对应里程碑内关掉,并回写到文档
- 重大技术选择写 ADR,编号递增,不修改已接受的 ADR,只追加新的取代它
