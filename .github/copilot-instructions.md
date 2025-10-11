# Copilot Instructions for zmk-dongle-display-bongocaat

## 项目架构概览
- 本项目为 ZMK 键盘固件的扩展模块，主要实现自定义 OLED 屏幕显示（128x64 或 128x32）。
- 主要代码位于 `boards/shields/dongle_display/`，其中 `widgets/` 目录包含所有屏幕小部件（如电池、层状态、bongo cat 等）。
- 配置文件（如 `.conf`, `.overlay`, `.yml`）用于屏幕和固件参数设置。

## 关键开发流程
- **构建/编译**：通过修改 `build.yaml`，指定 `dongle_display` shield。通常结合 ZMK 官方仓库和本模块进行 west 构建。
- **配置**：通过 `dongle_display.conf`、`dongle_display.overlay`、`dongle_display.zmk.yml` 进行硬件和功能配置。
- **自定义功能**：可通过 Kconfig 文件（`Kconfig.defconfig`, `Kconfig.shield`）启用/禁用特定小部件或功能。
- **常用命令**：
  - west build -b <board> -- -DSHIELD=dongle_display
  - west flash

## 主要约定与模式
- 屏幕小部件均为 C 文件，命名为 `<widget>.c/.h`，并在 `widgets/` 目录下。
- 屏幕尺寸常量在 `util.h` 中定义（如 `CANVAS_SIZE_W`, `CANVAS_SIZE_H`），并在各小部件中统一引用。
- LVGL 颜色和样式通过宏定义（如 `LVGL_BACKGROUND`, `LVGL_FOREGROUND`）实现主题切换。
- 状态数据结构统一使用 `struct status_state`，便于跨小部件传递。
- 仅保留与 WPM（每分钟击键数）相关的辅助函数和配置在 `util.h`。

## 典型集成点
- 依赖 ZMK 主仓库（通过 west.yml 管理），需同步主固件更新。
- OLED 屏幕驱动兼容多种型号（如 SSD1306、SH1106），通过 overlay 文件和配置项切换。
- 支持多种键盘板载方案（如 nice!nano, seeeduino_xiao_ble），通过 build.yaml 和 overlay 文件灵活配置。

## 重要文件参考
- `README.md`：使用方法、硬件兼容性、配置示例
- `boards/shields/dongle_display/widgets/`：所有屏幕小部件实现
- `dongle_display.conf`、`dongle_display.overlay`：硬件参数与功能配置
- `Kconfig.*`：功能开关与默认参数
- `util.h`：通用辅助函数与常量

## 示例：添加新小部件
1. 在 `widgets/` 下新建 `<widget>.c/.h`，参考现有小部件结构。
2. 在主屏幕文件（如 `custom_status_screen.c`）中注册并调用新小部件。
3. 如需配置开关，更新 `Kconfig.shield` 并在 overlay/conf 文件中添加对应项。

---
如有不清楚或遗漏的部分，请反馈以便进一步完善说明。