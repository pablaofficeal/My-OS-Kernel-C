#!/bin/bash
echo "🔧 Building PureC OS..."
rm -rf iso/
rm -f *.o *.bin *.iso

echo "📦 Compiling modules..."
# Компилируем все файлы по одному для ясности
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I. -c src/kernel.c -o kernel.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I. -c src/drivers/screen.c -o screen.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I. -c src/drivers/keyboard.c -o keyboard.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I. -c src/lib/string.c -o string.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I. -c src/shell/shell.c -o shell.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I. -c src/shell/commands.c -o commands.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I. -c src/fs/disk.c -o disk.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I. -c src/fs/fat16.c -o fat16.o

echo "🔗 Linking kernel..."
ld -m elf_i386 -T linker.ld -o kernel.bin kernel.o screen.o keyboard.o string.o shell.o commands.o disk.o fat16.o

if [ ! -f kernel.bin ]; then
    echo "❌ Linking failed! Check for errors above."
    exit 1
fi

echo "📀 Creating ISO structure..."
mkdir -p iso/boot/grub
cp kernel.bin iso/boot/

# Создаем конфигурацию GRUB
cat > iso/boot/grub/grub.cfg << 'EOF'
set timeout=5
set default=0

menuentry "PureC OS - FAT16 File System" {
    multiboot /boot/kernel.bin
    boot
}
EOF

echo "🎯 Creating bootable ISO..."

# Пробуем разные методы создания ISO
if command -v grub-mkrescue &> /dev/null; then
    echo "Using grub-mkrescue..."
    grub-mkrescue -o myos.iso iso/ 2>/dev/null
elif command -v xorriso &> /dev/null; then
    echo "Using xorriso..."
    xorriso -as mkisofs -R -b boot/grub/stage2_eltorito -no-emul-boot -boot-load-size 4 -boot-info-table -o myos.iso iso/
else
    echo "❌ No ISO creation tool found!"
    echo "💡 Install one of: grub-mkrescue, xorriso, or genisoimage"
    echo "Ubuntu/Debian: sudo apt install grub2-common xorriso"
    echo "Fedora: sudo dnf install grub2-tools xorriso"
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
qemu-system-i386 -cdrom myos.iso -m 512M