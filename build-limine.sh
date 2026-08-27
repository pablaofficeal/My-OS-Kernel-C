#!/bin/bash
set -e
# build-limine.sh - PureC OS Limine ISO (BIOS+UEFI, GOP)
# Требует: limine (pacman -S limine), xorriso, mtools
# Использует kernel.elf из ./build.sh (hybrid ENTRY _start + multiboot)

echo "🔨 Building Limine kernel (higher half, GOP)..."
# Пересобираем kernel-limine.elf с higher half (0xffffffff80000000)
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/boot/boot.c -o boot_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/kernel.c -o kernel_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/system_info.c -o system_info_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/arch/x86_64/gdt.c -o gdt_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/arch/x86_64/idt.c -o idt_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/syscall.c -o syscall_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/serial.c -o serial_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/vga.c -o vga_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/pic.c -o pic_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/fb.c -o fb_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/gop.c -o gop_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/mouse/ps2_mouse.c -o ps2_mouse_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/lib/string.c -o string_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/klog.c -o klog_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/boot_diag.c -o boot_diag_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/panic.c -o panic_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/keyboard.c -o keyboard_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/pci/pci.c -o pci_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/storage/ata_pio.c -o ata_pio_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/storage/ahci.c -o ahci_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/storage/block_device.c -o block_device_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/storage/storage_probe.c -o storage_probe_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/usb/xhci.c -o xhci_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/usb/ehci.c -o ehci_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/fs/fat32.c -o fat32_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/userspace.c -o userspace_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/terminal/terminal.c -o terminal_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/terminal/commands.c -o commands_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/terminal/shell_path.c -o shell_path_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/editor/nano.c -o nano_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/games/snake.c -o snake_limine.o
nasm -f elf64 src/arch/x86_64/gdt.asm -o gdt_asm_limine.o
nasm -f elf64 src/arch/x86_64/idt.asm -o idt_asm_limine.o
x86_64-elf-ld -T linker-limine.ld -o kernel-limine.elf boot_limine.o kernel_limine.o system_info_limine.o gdt_limine.o idt_limine.o syscall_limine.o serial_limine.o vga_limine.o pic_limine.o fb_limine.o gop_limine.o ps2_mouse_limine.o string_limine.o klog_limine.o boot_diag_limine.o panic_limine.o keyboard_limine.o pci_limine.o ata_pio_limine.o ahci_limine.o xhci_limine.o ehci_limine.o block_device_limine.o storage_probe_limine.o fat32_limine.o userspace_limine.o terminal_limine.o commands_limine.o shell_path_limine.o nano_limine.o snake_limine.o gdt_asm_limine.o idt_asm_limine.o
echo "✅ kernel-limine.elf: $(file kernel-limine.elf | cut -d: -f2)"
x86_64-elf-readelf -l kernel-limine.elf | head -n12

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

cp kernel-limine.elf iso_limine/boot/kernel.elf
cat > iso_limine/boot/limine/limine.conf <<'EOF'
timeout: 3
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

ls -lh purec_limine.iso kernel-limine.elf
echo "✅ purec_limine.iso готов (higher half, no Lower half PHDR panic)"
echo "🚀 BIOS QEMU:  qemu-system-x86_64 -cdrom purec_limine.iso -m 512M -nographic"
echo "🚀 UEFI QEMU:  qemu-system-x86_64 -bios /usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=/tmp/ovmf_vars.fd -cdrom purec_limine.iso -m 512M -nographic -serial file:serial.log"
echo "   GOP активен только в UEFI Limine (framebuffer), в BIOS fallback на VGA"
