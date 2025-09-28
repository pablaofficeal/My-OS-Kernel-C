#!/bin/bash
echo "🔧 Building PureC OS with Snake Game..."
rm -rf iso/
rm -f *.o *.bin *.iso

echo "📦 Compiling modules..."
# Компилируем все файлы с правильными путями
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/kernel.c -o kernel.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/screen.c -o screen.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/keyboard.c -o keyboard.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/lib/string.c -o string.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/shell/shell.c -o shell.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/shell/commands.c -o commands.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/fs/disk.c -o disk.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/fs/fat16.c -o fat16.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/game/snake.c -o snake.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/pci/pci.c -o pci.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/wifi/wifi.c -o wifi.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/wifi/intel_ax210.c -o ax210.o

echo "🔗 Linking kernel..."
ld -m elf_i386 -T linker.ld -o kernel.bin kernel.o screen.o keyboard.o string.o shell.o commands.o disk.o fat16.o snake.o pci.o wifi.o ax210.o

if [ ! -f kernel.bin ]; then
    echo "❌ Linking failed! Check for errors above."
    exit 1
fi

echo "📀 Creating ISO..."
mkdir -p iso/boot/grub
cp kernel.bin iso/boot/

cat > iso/boot/grub/grub.cfg << 'EOF'
set timeout=5
set default=0

menuentry "PureC OS - FAT16 with Snake" {
    multiboot /boot/kernel.bin
    boot
}
EOF

grub-mkrescue -o myos.iso iso/ 2>/dev/null

if [ ! -f myos.iso ]; then
    echo "❌ ISO creation failed!"
    exit 1
fi

echo "✅ Build successful!"
echo "📁 Generated: myos.iso ($(du -h myos.iso | cut -f1))"
echo ""
echo "🎮 Now available: 'snake' command"
echo "🚀 Starting QEMU..."
qemu-system-i386 -cdrom myos.iso -m 512M