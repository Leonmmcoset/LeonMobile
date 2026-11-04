#!/usr/bin/env bash
set -euo pipefail
target=$1
mkdir -p "$target/media" "$target/data" "$target/var" "$target/run" "$target/tmp" "$target/usr/share/leon/apps"
chmod 0755 "$target/usr/libexec/leon/"*
chmod 0644 "$target/etc/leon/services/"*
ln -sfn /sbin/leon-init "$target/sbin/init"
ln -sfn /usr/sbin/leon-init "$target/sbin/leon-init"
ln -sfn /usr/sbin/leond "$target/sbin/leond"
rm -f "$target/etc/init.d/rcS" "$target/etc/inittab"
cat > "$target/etc/os-release" <<'EOF'
NAME="LeonMobile"
ID=leonmobile
VERSION="1.0.0"
VERSION_ID=1.0.0
PRETTY_NAME="LeonMobile 1.0.0 (QEMU developer prototype)"
EOF
# /etc is read-only. Network resolver state lives on tmpfs.
rm -f "$target/etc/resolv.conf"
ln -s /run/resolv.conf "$target/etc/resolv.conf"
rm -f "$target/etc/localtime"
ln -s /run/localtime "$target/etc/localtime"
mkdir -p "$target/etc/fonts/conf.d"
cat > "$target/etc/fonts/local.conf" <<'EOF'
<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig><cachedir>/var/cache/fontconfig</cachedir><alias><family>sans-serif</family><prefer><family>DejaVu Sans</family><family>WenQuanYi Zen Hei</family></prefer></alias><alias><family>monospace</family><prefer><family>DejaVu Sans Mono</family><family>WenQuanYi Zen Hei Mono</family><family>WenQuanYi Zen Hei</family></prefer></alias></fontconfig>
EOF

# Retire the prototype dispatcher when rebuilding an existing output tree.
rm -f "$target/usr/bin/leon-app"

# Root login stays locked even when rebuilding an older output directory.
# User elevation is mediated by rootd, never by a distro password or sudo.
sed -i 's/^root:[^:]*:/root:!:/' "$target/etc/shadow"
chmod 0600 "$target/etc/shadow"
rm -f "$target/bin/su" "$target/sbin/sulogin" "$target/usr/bin/sudoedit"
chmod 0755 "$target/usr/bin/sudo"
ln -s sudo "$target/usr/bin/sudoedit"
