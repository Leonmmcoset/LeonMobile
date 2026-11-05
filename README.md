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

终端运行真实 PTY 交互式 shell，采用等宽字符网格和 libvterm，支持常用 ANSI/VT 控制序列、256 色/真彩色、Unicode、全屏程序、历史滚动、选择复制粘贴及移动端控制键。镜像包含等宽字体、terminfo、`vi`、`top`、`less` 和 `stty`，使用方式与协议范围见[终端说明](docs/terminal.md)。

设置采用安卓风格的大标题、搜索栏和分类列表，包含网络、显示、声音、存储、系统及关于设备页面。`settingsd` 统一保存主题、播放音量、语言和导航方式；网络页支持以太网启停、DHCP 续租、静态 IPv4 和 DNS，声音页显示实际输出及应用音量。

“设置 → 系统 → 日期与时间”支持自动校时、手动日期时间、时区搜索、12/24 小时制与最近校时结果。“设置 → 通知管理”支持逐应用通知/提示音开关、勿扰和锁屏内容隐私；策略重启保留，提示音通过 audiod 播放。使用及验证说明见[日期时间与通知设置](docs/date-time-notifications.md)。

从顶栏下拉可在当前应用上打开通知面板，再下拉展开完整控制菜单；支持拖动跟随、主题切换、音量与静音、锁屏、设置入口和电源菜单。向上收起或点面板外侧可返回原应用。

通知由独立的 `notificationd` 管理，桌面和内置应用通过 `/run/leon/notifications.sock` 通信，旧应用接口保留转发兼容。服务按应用 UID 确认来源，内存中保存最近 64 条通知；通知消息目前没有持久化、手动清除、通知渠道或远程推送。

底层包括应用前后台、状态保存与超时退出协议，服务就绪握手与事件循环看门狗，以及独立的 `memd`、`powerd`、`storaged`、`netd`、`timed`、`crashd` 和 `portald`。应用启动时执行 Landlock、seccomp 和 cgroup 资源限制；文件导入经系统确认和文件选择器复制到应用私有目录。`imed` 通过 Wayland text-input-v3 / input-method-v2 提供统一中英文键盘与基础拼音候选。服务能力、权限、持久化和硬件限制见[核心系统服务](docs/core-services.md)。

`intentd` 根据应用声明处理打开文件、链接、分享和默认应用选择；`documentd` 通过系统文件选择器授予可过期、可撤销的单文件访问。`audiod` 代理应用 PCM 播放，结合 PipeWire 混音，管理音量、音频焦点和锁屏媒体控制；LMUI 展示提供测试音与文本分享。设置 → 系统可查看默认应用并撤销临时文件授权。协议、SDK 和当前边界见[平台服务](docs/platform-services.md)。

Root 提权默认关闭。在“设置 → 系统 → 开发者选项”设置密码后启用；开关和密码保留到下次启动，每次 `su` 仍需验证密码，`sudo` 始终拒绝。独立 `rootd` 提供 root PTY、运行时内核参数、模块加载/卸载、服务控制及持久化启动/重启策略；开发者页面同时显示核心组件状态。第三方应用须声明专用 helper，并经系统确认和 Root 密码验证；会话关闭或权限撤销时清理对应进程。详细边界和验证方式见 [Root 管理](docs/root-access.md)。

设备锁屏使用独立 `authd`，在“设置 → 系统 → 安全与屏幕锁定”设置、修改或关闭 PIN／密码，与 Root 密码独立。配置后，开机、手动与空闲锁定均需认证；连续输错的等待限制保留到服务重启和系统重启后。合成器通过一次性凭证解锁，在认证服务失联或桌面尚未提交锁屏画面时遮住应用并清除输入焦点。详见 [设备认证](docs/device-authentication.md)。

LMUI 与系统界面使用 Material 3 非线性动效，覆盖触控涟漪、开关与滑块、页面和键盘切换、锁屏回弹、通知面板及任务重排；动画可中途打断，滑动控件直接跟手，结束后停止动画重绘。设置 → 显示 → 减少动态效果可关闭位移与扩散动画并保留即时按压反馈，开发接口见[动效与触控说明](docs/motion.md)。

应用默认使用独立的挂载、PID、IPC 命名空间，私有临时目录和 PTY，以及最小设备视图；保留 Unix UID、Landlock、seccomp 和 cgroup 限制。隔离范围、运行目录及清理机制见[应用隔离](docs/application-isolation.md)。

当前边界：仅 QEMU，尚未设置设备凭据时使用滑动锁屏；内核、网络命名空间、Wayland 与 AF_UNIX 仍共享，尚无独立安全审计；软件包未签名；系统通过离线替换镜像更新；尚无实体手机、动态壁纸取色、Android 兼容、应用商店或 OTA。

自研用户态组件使用 Apache-2.0。基于 tinywl 的合成器基础代码遵循 CC0-1.0，见 `third_party/tinywl-LICENSE`；其余第三方代码遵循各自许可证。
