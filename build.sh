gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/start.c -o start.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/kernel.c -o kernel.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/screen.c -o screen.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/text_output.c -o text_output.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/keyboard/keyboard.c -o keyboard.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/lib/string.c -o string.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/lib/memory.c -o memory.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/shell/shell.c -o shell.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/shell/commands.c -o commands.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/fs/disk.c -o disk.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/fs/fat16.c -o fat16.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/tools/hexedit.c -o hexedit.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/game/snake/snake.c -o snake.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/game/tetris/tetris.c -o tetris.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/pci/pci.c -o pci.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/wifi/wifi.c -o wifi.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/wifi/intel_ax210.c -o ax210.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/usb/usb_driver.c -o usb_driver.o

ld -m elf_i386 -T linker.ld -o kernel.bin \
    start.o kernel.o screen.o text_output.o keyboard.o string.o memory.o \
    shell.o commands.o \
    disk.o fat16.o \
    hexedit.o \
    snake.o tetris.o \
    pci.o wifi.o ax210.o usb_driver.o

if [ ! -f kernel.bin ]; then
    echo "❌ Linking failed! Check for errors above."
    exit 1
fi

mkdir -p iso/boot/grub
cp kernel.bin iso/boot/

cat > iso/boot/grub/grub.cfg << 'EOF'
set timeout=5
set default=0

# Устанавливаем текстовый режим для совместимости
set gfxpayload=text

# Опции для отладки и совместимости
set debug=multiboot

# Включаем консоль для решения проблемы "no console"
terminal_input console
terminal_output console

menuentry "PureC OS" {
    insmod vbe
    insmod vga
    insmod video_bochs
    insmod video_cirrus
    multiboot /boot/kernel.bin
    boot
}

# Резервный вариант с минимальными требованиями
menuentry "PureC OS (Safe Mode)" {
    set gfxpayload=text
    terminal_input console
    terminal_output console
    multiboot /boot/kernel.bin
    boot
}
EOF

grub-mkrescue -o myos.iso iso/ 2>/dev/null

if [ ! -f myos.iso ]; then
    echo "❌ ISO creation failed!"
    exit 1
fi