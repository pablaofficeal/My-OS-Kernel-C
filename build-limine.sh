#!/bin/bash
set -e
# build-limine.sh - PureC OS Limine ISO (BIOS+UEFI, GOP)
# Требует: limine (pacman -S limine), xorriso, mtools
# Использует kernel.elf из ./build.sh (hybrid ENTRY _start + multiboot)

if [ ! -f kernel.elf ]; then
  echo "⚠️  kernel.elf не найден — запускаю ./build.sh"
  bash ./build.sh
fi

LIMINE_SHARE=""
for p in /usr/share/limine /tmp/limine-pkg/usr/share/limine; do
  [ -d "$p" ] && LIMINE_SHARE="$p" && break
done
if [ -z "$LIMINE_SHARE" ]; then
  echo "❌ limine не найден. Установи: sudo pacman -S limine"
  echo "   или скачай: wget https://archlinux.org/packages/extra/x86_64/limine/download -O /tmp/limine.pkg.tar.zst && mkdir -p /tmp/limine-pkg && tar -I zstd -xf /tmp/limine.pkg.tar.zst -C /tmp/limine-pkg"
  exit 1
fi
LIMINE_BIN=""
for p in /usr/bin/limine /tmp/limine-pkg/usr/bin/limine; do
  [ -f "$p" ] && LIMINE_BIN="$p" && break
done

echo "📦 Limine: $LIMINE_SHARE"
rm -rf iso_limine
mkdir -p iso_limine/boot/limine iso_limine/EFI/BOOT

cp kernel.elf iso_limine/boot/kernel.elf
cat > iso_limine/boot/limine/limine.conf <<'EOF'
timeout: 0
verbose: yes
serial: yes
/PureC OS 64-bit Limine+GOP
    protocol: limine
    kernel_path: boot():/boot/kernel.elf
EOF
# Fallback для поиска конфига
cp iso_limine/boot/limine/limine.conf iso_limine/limine.conf

cp "$LIMINE_SHARE/limine-bios.sys" iso_limine/boot/limine/ 2>/dev/null || true
cp "$LIMINE_SHARE/limine-bios-cd.bin" iso_limine/boot/limine/ 2>/dev/null || true
cp "$LIMINE_SHARE/limine-uefi-cd.bin" iso_limine/boot/limine/ 2>/dev/null || true
cp "$LIMINE_SHARE/BOOTX64.EFI" iso_limine/EFI/BOOT/ 2>/dev/null || true
cp "$LIMINE_SHARE/BOOTIA32.EFI" iso_limine/EFI/BOOT/ 2>/dev/null || true
# Дублируем bios.sys в корень для надежности
cp "$LIMINE_SHARE/limine-bios.sys" iso_limine/ 2>/dev/null || true
cp "$LIMINE_SHARE/limine-bios.sys" iso_limine/boot/ 2>/dev/null || true

echo "📀 xorriso (BIOS+UEFI)..."
xorriso -as mkisofs \
  -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
  --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label \
  iso_limine -o purec_limine.iso 2>&1 | tail -5

echo "🔧 limine bios-install..."
chmod +x "$LIMINE_BIN" 2>/dev/null || true
"$LIMINE_BIN" bios-install purec_limine.iso 2>&1 | tail -5

ls -lh purec_limine.iso kernel.elf
echo "✅ purec_limine.iso готов"
echo "🚀 BIOS QEMU:  qemu-system-x86_64 -cdrom purec_limine.iso -m 512M -nographic"
echo "🚀 UEFI QEMU:  qemu-system-x86_64 -bios /usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=/tmp/ovmf_vars.fd -cdrom purec_limine.iso -m 512M -nographic -serial file:serial.log"
echo "   GOP активен только в UEFI Limine (framebuffer), в BIOS fallback на VGA"
