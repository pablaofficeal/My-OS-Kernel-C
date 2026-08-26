#!/bin/bash
set -e
echo "🧹 Cleaning..."
rm -rf iso kernel.elf purec_os.iso *.o
mkdir -p iso/boot/limine iso/EFI/BOOT

# Use Limine binaries from Arch pkg if available
LIMINE_DIR="/tmp/limine-pkg/usr/share/limine"
if [ ! -d "$LIMINE_DIR" ]; then
  if [ -f /tmp/limine.pkg.tar.zst ]; then
    mkdir -p /tmp/limine-pkg && tar -I zstd -xf /tmp/limine.pkg.tar.zst -C /tmp/limine-pkg 2>/dev/null || true
    LIMINE_DIR="/tmp/limine-pkg/usr/share/limine"
  fi
fi
LIMINE_BIN="/tmp/limine-pkg/usr/bin/limine"
if [ ! -f "$LIMINE_DIR/BOOTX64.EFI" ]; then
  echo "❌ Limine binaries not found, trying /tmp/limine-8.3.1..."
  LIMINE_DIR="/tmp/limine-8.3.1"
fi

echo "📦 Limine dir: $LIMINE_DIR"
ls "$LIMINE_DIR" | head -n10

echo "🔨 Building kernel (x86_64-elf, -mcmodel=kernel)..."
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/boot/boot.c -o boot.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/kernel.c -o kernel.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/arch/x86_64/gdt.c -o gdt.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/arch/x86_64/idt.c -o idt.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/fb.c -o fb.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/serial.c -o serial.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/lib/string.c -o string.o
nasm -f elf64 src/arch/x86_64/gdt.asm -o gdt_asm.o
nasm -f elf64 src/arch/x86_64/idt.asm -o idt_asm.o

echo "🔗 Linking..."
x86_64-elf-ld -T linker.ld -o kernel.elf boot.o kernel.o gdt.o idt.o fb.o serial.o string.o gdt_asm.o idt_asm.o
echo "✅ kernel.elf: $(file kernel.elf | cut -d: -f2)"

echo "📦 ISO layout..."
cp kernel.elf iso/boot/
cat > iso/boot/limine/limine.conf << 'EOF'
timeout: 0
verbose: yes

/PureC OS 64-bit
    protocol: limine
    kernel_path: boot():/boot/kernel.elf
EOF
cat iso/boot/limine/limine.conf
cp "$LIMINE_DIR/limine-bios.sys" iso/boot/limine/ 2>/dev/null || cp "$LIMINE_DIR/../limine-bios.sys" iso/boot/limine/ || true
cp "$LIMINE_DIR/limine-bios-cd.bin" iso/boot/limine/ 2>/dev/null || true
cp "$LIMINE_DIR/limine-uefi-cd.bin" iso/boot/limine/ 2>/dev/null || true
cp "$LIMINE_DIR/BOOTX64.EFI" iso/EFI/BOOT/BOOTX64.EFI 2>/dev/null || true
cp "$LIMINE_DIR/BOOTIA32.EFI" iso/EFI/BOOT/BOOTIA32.EFI 2>/dev/null || true
ls -R iso | head -n30

echo "📀 Creating ISO..."
xorriso -as mkisofs -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label iso -o purec_os.iso 2>&1 | tail -10

echo "🔧 BIOS install..."
if [ -f "$LIMINE_BIN" ]; then chmod +x "$LIMINE_BIN"; "$LIMINE_BIN" bios-install purec_os.iso 2>&1 | tail -5; else echo "no limine bin"; fi

echo "✅ Build complete: purec_os.iso ($(du -h purec_os.iso | cut -f1))"
ls -lh kernel.elf purec_os.iso
echo "🚀 BIOS: qemu-system-x86_64 -cdrom purec_os.iso -m 512M -serial stdio -display gtk"
echo "🚀 UEFI: qemu-system-x86_64 -bios /usr/share/edk2-ovmf/x64/OVMF.4m.fd -cdrom purec_os.iso -m 512M -serial stdio -display gtk"
echo "🚀 UEFI+serial: qemu-system-x86_64 -bios /usr/share/edk2-ovmf/x64/OVMF.4m.fd -cdrom purec_os.iso -m 512M -serial file:qemu_logs.txt -display none"
