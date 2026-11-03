# LeonMobile

LeonMobile 1.0.0 是面向 ARM64 的 Linux 移动操作系统图形原型，首先支持 QEMU `virt`。系统采用 Linux 7.3.1、musl、ext4、Wayland、自研移动桌面及 Material You 风格的 LMUI。

这是面向开发的图形原型；版本号表示目标发行版本，不表示生产级手机系统认证。

- `distro/`：Buildroot 2026.08.1 外部配置层、设备配置、软件包和根文件系统配置。
- `src/system/`：C17 自研 PID 1、服务管理器和管理命令。
- `src/pkg/`：原生 ELF 应用包安装器及应用启动服务。
- `src/desktop/`、`src/lmui/`、`src/apps/`：移动桌面、C++20 UI 框架和系统应用。
- `scripts/`、`tests/`、`docs/`：构建工具、验证和接口文档。

仅验证系统及应用管理代码：

```sh
cmake -S . -B build/core -G Ninja -DLEON_GRAPHICS=OFF
cmake --build build/core
ctest --test-dir build/core --output-on-failure
```

构建目标系统：`make image`。第三方源码及构建产物存放于忽略的 `.cache/`、`build/` 和 `output/`。

```sh
make test                     # 原生编译、组件及安装事务测试
./scripts/test-wayland.sh      # 在独立 Wayland 服务端中渲染桌面与应用
make image                    # 完整 ARM64 / musl 系统镜像
make run                      # 使用持久化磁盘副本启动 QEMU
python3 scripts/test-qemu.py   # 目标系统启动与图形交互验收
./scripts/build-example.sh    # 编译并打包原生应用示例
make release                  # 汇集镜像、清单、许可证和校验和
```

详细说明见 [构建与运行](docs/building.md)、[系统架构](docs/architecture.md)、[服务及 LMUI 接口](docs/interfaces.md)和[应用包规范](docs/application-format.md)。

已完成完整 ARM64 镜像构建、原生组件及集成测试、独立 Wayland 渲染和 QEMU 图形交互验收，包括示例应用包的安装、运行、卸载及界面关机。具体范围见[验收记录](docs/validation.md)。本地发行物位于 `dist/leonmobile-1.0.0-qemu-aarch64/`，可在该目录执行 `./run.sh` 启动。

首版包括锁屏、主屏、应用列表、任务切换、状态栏、通知、快捷设置、屏幕键盘，以及设置、计算器、LMUI 展示、文件、文本编辑、终端、图片和时钟应用。采用固定 Material 风格配色，可切换深浅主题、中文/英文和手势/三键导航。

当前边界：仅 QEMU；滑动锁屏无身份认证；应用隔离基于 Unix UID；软件包未签名；系统通过离线替换镜像更新；尚无实体手机、动态壁纸取色、Android 兼容、应用商店或 OTA。

自研用户态组件使用 Apache-2.0。基于 tinywl 的合成器基础代码遵循 CC0-1.0，见 `third_party/tinywl-LICENSE`；其余第三方代码遵循各自许可证。
