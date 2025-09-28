#!/bin/bash

echo "Очистка предыдущих файлов..."
rm -f *.bin *.o

echo "Сборка загрузчика..."
nasm -f bin boot.asm -o boot.bin

echo "Сборка ядра..."
# Компилируем напрямую в бинарный формат
gcc -m32 -ffreestanding -fno-pie -nostdlib -c kernel.c -o kernel.o
objcopy -O binary kernel.o kernel.bin

echo "Создание образа..."
# Создаем образ правильного размера
dd if=/dev/zero of=os-image.bin bs=512 count=2880 2>/dev/null
dd if=boot.bin of=os-image.bin conv=notrunc 2>/dev/null
dd if=kernel.bin of=os-image.bin conv=notrunc bs=512 seek=1 2>/dev/null

echo "Размеры файлов:"
ls -la *.bin

echo "Запуск QEMU..."
qemu-system-i386 -drive file=os-image.bin,format=raw,if=floppy -no-reboot