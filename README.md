# LeonMobile

LeonMobile 1.0.0 是面向 ARM64 的 Linux 移动操作系统图形原型，首先支持 QEMU `virt`。系统采用 Linux 7.3.1、musl、ext4、Wayland、自研移动桌面及 Material You 风格的 LMUI。

项目正在实现中；版本号表示目标发行版本，不表示已经通过发布验收。

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

自研用户态组件使用 Apache-2.0。基于 tinywl 的合成器基础代码遵循 CC0-1.0，见 `third_party/tinywl-LICENSE`；其余第三方代码遵循各自许可证。
