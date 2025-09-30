#!/bin/bash
rm -rf iso/
rm -f *.o *.bin *.iso

gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/kernel.c -o kernel.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/screen.c -o screen.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/keyboard.c -o keyboard.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/lib/string.c -o string.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/shell/shell.c -o shell.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/shell/commands.c -o commands.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/fs/disk.c -o disk.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/fs/fat16.c -o fat16.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/game/snake/snake.c -o snake.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/pci/pci.c -o pci.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/wifi/wifi.c -o wifi.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/wifi/intel_ax210.c -o ax210.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/game/tetris/tetris.c -o tetris.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/game/2048/game_common.c -o game_common.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/game/2048/field_4x4.c -o field_4x4.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/game/2048/field_8x8.c -o field_8x8.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/game/2048/field_16x16.c -o field_16x16.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/game/2048/game_start.c -o game_start.o

ld -m elf_i386 -T linker.ld -o kernel.bin kernel.o screen.o keyboard.o string.o shell.o commands.o disk.o fat16.o snake.o pci.o wifi.o ax210.o tetris.o game_common.o field_4x4.o field_8x8.o field_16x16.o game_start.o

if [ ! -f kernel.bin ]; then
    echo "❌ Linking failed! Check for errors above."
    exit 1
fi

mkdir -p iso/boot/grub
cp kernel.bin iso/boot/

cat > iso/boot/grub/grub.cfg << 'EOF'
set timeout=5
set default=0

menuentry "PureC OS - Wi-Fi & Snake" {
    multiboot /boot/kernel.bin
    boot
}
EOF

grub-mkrescue -o myos.iso iso/ 2>/dev/null

if [ ! -f myos.iso ]; then
    echo "❌ ISO creation failed!"
    exit 1
fi
rm -f *.o*
qemu-system-i386 -cdrom myos.iso -m 512M