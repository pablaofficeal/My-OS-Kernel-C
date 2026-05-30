gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/start.c -o start.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/boot/lowlevel.c -o lowlevel.o
# Компилируем ассемблерный файл
nasm -f elf32 src/boot/lowlevel.asm -o lowlevel_asm.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/kernel.c -o kernel.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/screen.c -o screen.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/text_output.c -o text_output.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/drivers/keyboard/keyboard.c -o keyboard.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/lib/string.c -o string.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/lib/memory.c -o memory.o
gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 -I./src -c src/lib/error_handler.c -o error_handler.o
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
# Компилируем драйвер мыши на ассемблере
nasm -f elf32 src/drivers/mouse/mouse.asm -o mouse.o
# Компилируем загрузчик (для создания boot.bin, если нужен отдельный загрузчик)
nasm -f bin src/boot/boot.asm -o boot.bin
# Создаем ELF-файл сначала
ld -m elf_i386 -T linker.ld -o kernel.elf \
    start.o lowlevel.o lowlevel_asm.o kernel.o screen.o text_output.o keyboard.o string.o memory.o error_handler.o \
    shell.o commands.o \
    disk.o fat16.o \
    hexedit.o \
    snake.o tetris.o \
    pci.o wifi.o ax210.o usb_driver.o \
    mouse.o


if [ ! -f kernel.elf ]; then
    echo "❌ Linking failed! Check for errors above."
    exit 1
fi

# GRUB Multiboot требует ELF формат, не flat binary
# Но для совместимости создаем и binary тоже
objcopy -O binary kernel.elf kernel.bin

if [ ! -f kernel.elf ]; then
    echo "❌ Kernel ELF not found!"
    exit 1
fi

mkdir -p iso/boot/grub
# Копируем ELF для GRUB Multiboot
cp kernel.elf iso/boot/kernel.elf
# И binary для совместимости
cp kernel.bin iso/boot/kernel.bin
mkdir -p iso/EFI/BOOT

# Создаем UEFI загрузочный файл
echo 'GRUB boot file for UEFI' > iso/EFI/BOOT/BOOTX64.EFI

# Создаем BIOS-совместимую конфигурацию
cat > iso/boot/grub/grub.cfg << 'EOF'
set timeout=10
set default=0

# Базовые модули для совместимости
insmod part_gpt
insmod part_msdos
insmod fat
insmod multiboot
insmod multiboot2

# Опции загрузки по умолчанию
set gfxpayload=text

menuentry "PureC OS (Multiboot2)" {
    echo "Loading PureC OS kernel with Multiboot2..."
    multiboot2 /boot/kernel.elf
    boot
}

menuentry "PureC OS (Multiboot)" {
    echo "Loading PureC OS kernel with Multiboot..."
    multiboot /boot/kernel.elf
    boot
}

menuentry "PureC OS (Safe Mode)" {
    echo "Loading PureC OS kernel in safe mode..."
    set gfxpayload=text
    multiboot2 /boot/kernel.elf
    boot
}

menuentry "PureC OS (Legacy Fallback)" {
    echo "Loading PureC OS kernel (legacy fallback)..."
    multiboot /boot/kernel.elf
    boot
}
EOF

# Создаем UEFI-специфичную конфигурацию
cat > iso/EFI/BOOT/grub.cfg << 'EOF'
set timeout=10
set default=0

# Отключаем графику для UEFI
set gfxpayload=text

# Базовые настройки для совместимости
insmod part_gpt
insmod part_msdos
insmod fat
insmod multiboot
insmod multiboot2

menuentry "PureC OS (UEFI)" {
    echo "Loading PureC OS kernel..."
    multiboot2 /boot/kernel.elf
    boot
}

menuentry "PureC OS (UEFI Safe Mode)" {
    echo "Loading PureC OS kernel (safe mode)..."
    multiboot2 /boot/kernel.elf
    boot
}

menuentry "PureC OS (Legacy Fallback)" {
    echo "Loading PureC OS kernel (legacy)..."
    multiboot /boot/kernel.elf
    boot
}
EOF

grub-mkrescue -o myos.iso iso/ 2>/dev/null

if [ ! -f myos.iso ]; then
    echo "❌ ISO creation failed!"
    exit 1
fi
#mkdir -p мусор/
#mv kernel.o мусор/
#mv start.o мусор/
#mv screen.o мусор/
#mv text_output.o мусор/
#mv keyboard.o мусор/
#mv string.o мусор/
#mv memory.o мусор/
#mv error_handler.o мусор/
#mv shell.o мусор/
#mv commands.o мусор/
#mv disk.o мусор/
#mv fat16.o мусор/
#mv hexedit.o мусор/
#mv snake.o мусор/
#mv tetris.o мусор/
#mv pci.o мусор/
#mv wifi.o мусор/
#mv ax210.o мусор/
#mv usb_driver.o мусор/

#sleep 3

#rm -rf мусор/

rm -rf *.o