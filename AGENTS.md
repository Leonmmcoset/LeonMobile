# LeonMobile development rules

- The user requires a Git commit after every completed, independently reviewable implementation step. Run the checks appropriate to that step, then commit with a concrete description and accurate validation status. Keep the working tree clean when handing work back.
- Preserve the accepted platform: LeonMobile 1.0.0, Linux 7.3.1, AArch64, musl, ext4, Wayland/wlroots, C17 system services, C++20 LMUI and mobile shell, Buildroot distribution integration.
- Treat 1.0.0 as a developer graphics prototype. Do not describe unverified target behavior as working or claim production phone support.
- Use the independent native Wayland smoke test for client changes and the QEMU target test for boot, compositor, image or runtime integration changes. Keep downloads, toolchains, disk images and build evidence outside version control.
- Document public manifest, service and SDK changes. Keep third-party notices and source hashes intact.
