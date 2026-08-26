#!/bin/bash
set -e

echo "🧹 Cleaning old files..."
rm -f *.o kernel.elf kernel.bin purec_os.iso

echo "🔨 Building assembly files..."
nasm -f elf32 src/boot/lowlevel.asm -o lowlevel_asm.o

echo "🔨 Building C files..."
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/start.c -o start.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/kernel.c -o kernel.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/lib/memory.c -o memory.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/screen.c -o screen.o

echo "🔗 Linking kernel (32-bit i386 ELF)..."
ld -m elf_i386 -T linker.ld -o kernel.elf start.o kernel.o memory.o screen.o lowlevel_asm.o
# Verify that it's a valid i386 ELF multiboot kernel for GRUB
grub-file --is-x86-multiboot kernel.elf && echo "✅ GRUB will recognize the multiboot kernel" || echo "⚠️  Warning: GRUB multiboot check failed"

echo "📦 Creating binary file..."
objcopy -O binary kernel.elf kernel.bin

echo "📀 Creating ISO image..."
mkdir -p iso/boot/grub
cp kernel.elf iso/boot/kernel.elf

cat > iso/boot/grub/grub.cfg << EOF
set timeout=0
set default=0

menuentry "PureC OS" {
    echo "Loading PureC OS..."
    multiboot /boot/kernel.elf
    boot
}
EOF

grub-mkrescue -o purec_os.iso iso/

echo "✅ Build complete! Image: purec_os.iso"
echo "🚀 Run WITH LOGS: qemu-system-i386 -m 512M -no-reboot -nographic -cdrom purec_os.iso -serial file:qemu_logs.txt"
echo "⚠️  -no-reboot forces QEMU to exit instead of rebooting, so logs are written correctly!"