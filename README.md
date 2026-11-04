# LeonMobile

LeonMobile 1.0.0 是面向 ARM64 的 Linux 移动操作系统图形原型，首先支持 QEMU `virt`。系统采用 Linux 7.3.1、musl、ext4、Wayland、自研移动桌面及 Material You 风格的 LMUI。

这是面向开发的图形原型；版本号表示目标发行版本，不表示生产级手机系统认证。

- `distro/`：Buildroot 2026.08.1 外部配置层、设备配置、软件包和根文件系统配置。
- `src/services/`：PID 1、服务管理器、应用、通知及内存、电源、存储、网络、时间、崩溃和权限服务；各自拥有独立入口和内部模块。
- `src/libraries/`：LMUI、应用公共组件、IPC 客户端和包管理库。
- `src/tools/`：`leonctl`、`lmpkg`、`lmsys` 管理命令。
- `src/desktop/`：独立的 Wayland 合成器、移动桌面和 `imed` 输入法工程。
- `src/apps/`：八个独立工程、独立清单、独立 ELF 的内置应用。
- `cmake/`：应用注册及构建规则。
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

详细说明见 [构建与运行](docs/building.md)、[系统架构](docs/architecture.md)、[组件开发](docs/development.md)、[核心系统服务](docs/core-services.md)、[服务及 LMUI 接口](docs/interfaces.md)和[应用包规范](docs/application-format.md)。

已完成完整 ARM64 镜像构建、原生组件及集成测试、独立 Wayland 渲染和 QEMU 图形交互验收，包括示例应用包的安装、运行、卸载及界面关机。具体范围见[验收记录](docs/validation.md)。本地发行物位于 `dist/leonmobile-1.0.0-qemu-aarch64/`，可在该目录执行 `./run.sh` 启动。

首版包括锁屏、主屏、应用列表、任务切换、状态栏、通知、快捷设置、屏幕键盘，以及设置、计算器、LMUI 展示、文件、文本编辑、终端、图片和时钟应用。采用固定 Material 风格配色，可切换深浅主题、中文/英文和手势/三键导航。

设置采用安卓风格的大标题、搜索栏和分类列表，包含网络、显示、声音、存储、系统及关于设备页面。主题、播放音量、语言和导航方式可直接调整；网络与存储页面显示实际接口和文件系统信息。

从顶栏下拉可在当前应用上打开通知面板，再下拉展开完整控制菜单；支持拖动跟随、主题切换、音量与静音、锁屏、设置入口和电源菜单。向上收起或点面板外侧可返回原应用。

通知由独立的 `notificationd` 管理，桌面和内置应用通过 `/run/leon/notifications.sock` 通信，旧应用接口保留转发兼容。服务按应用 UID 确认来源，内存中保存最近 64 条通知；目前没有持久化、清除、通知渠道或远程推送。

底层包括应用前后台、状态保存与超时退出协议，服务就绪握手与事件循环看门狗，以及独立的 `memd`、`powerd`、`storaged`、`netd`、`timed`、`crashd` 和 `portald`。应用启动时执行 Landlock、seccomp 和 cgroup 资源限制；文件导入经系统确认和文件选择器复制到应用私有目录。`imed` 通过 Wayland text-input-v3 / input-method-v2 提供统一中英文键盘与基础拼音候选。服务能力、权限、持久化和硬件限制见[核心系统服务](docs/core-services.md)。

LMUI 与系统界面使用 Material 3 非线性动效，覆盖触控涟漪、开关与滑块、页面和键盘切换、锁屏回弹、通知面板及任务重排；动画可中途打断，滑动控件直接跟手，结束后停止动画重绘。设置 → 显示 → 减少动态效果可关闭位移与扩散动画并保留即时按压反馈，开发接口见[动效与触控说明](docs/motion.md)。

当前边界：仅 QEMU；滑动锁屏无身份认证；应用使用 Unix UID、Landlock、seccomp 和 cgroup，尚无完整命名空间隔离或安全审计；软件包未签名；系统通过离线替换镜像更新；尚无实体手机、动态壁纸取色、Android 兼容、应用商店或 OTA。

自研用户态组件使用 Apache-2.0。基于 tinywl 的合成器基础代码遵循 CC0-1.0，见 `third_party/tinywl-LICENSE`；其余第三方代码遵循各自许可证。
