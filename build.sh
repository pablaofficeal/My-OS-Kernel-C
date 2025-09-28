#!/bin/bash
echo "🔧 Building PureC OS..."
rm -rf iso/
rm -f *.o *.bin *.iso

echo "📦 Compiling modules..."
# Компилируем с правильными флагами
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/kernel.c -o kernel.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/screen.c -o screen.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/keyboard.c -o keyboard.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/lib/string.c -o string.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/shell/shell.c -o shell.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/shell/commands.c -o commands.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/fs/disk.c -o disk.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/fs/fat16.c -o fat16.o

echo "🔗 Linking kernel..."
# УБИРАЕМ -nographic, добавляем --oformat binary если нужно
ld -m elf_i386 -T linker.ld -o kernel.bin kernel.o screen.o keyboard.o string.o shell.o commands.o disk.o fat16.o

if [ ! -f kernel.bin ]; then
    echo "❌ Linking failed! Check for errors above."
    exit 1
fi

echo "📀 Creating ISO structure..."
mkdir -p iso/boot/grub
cp kernel.bin iso/boot/

cat > iso/boot/grub/grub.cfg << 'EOF'
set timeout=5
set default=0

menuentry "PureC OS - FAT16 File System" {
    multiboot /boot/kernel.bin
    boot
}
EOF

echo "🎯 Creating bootable ISO..."
if command -v grub-mkrescue &> /dev/null; then
    echo "Using grub-mkrescue..."
    grub-mkrescue -o myos.iso iso/ 2>/dev/null
else
    echo "❌ grub-mkrescue not found!"
    echo "💡 Install: sudo apt install grub2-common xorriso"
    exit 1
fi

if [ ! -f myos.iso ]; then
    echo "❌ ISO creation failed!"
    exit 1
fi

echo "✅ Build successful!"
echo "📁 Generated: myos.iso ($(du -h myos.iso | cut -f1))"
echo ""
echo "🚀 To run: qemu-system-i386 -cdrom myos.iso"
echo "🎮 Starting QEMU..."
qemu-system-i386 -cdrom myos.iso -m 512M -serial stdio