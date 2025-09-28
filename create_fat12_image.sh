# create_fat12_image.sh
#!/bin/bash

echo "Creating FAT12 disk image..."

# Создаем образ дискеты 1.44MB
dd if=/dev/zero of=disk.img bs=512 count=2880

# Форматируем в FAT12
mkfs.fat -F12 disk.img

# Монтируем и создаем тестовые файлы
sudo mkdir -p /mnt/myos
sudo mount disk.img /mnt/myos

# Создаем тестовые файлы
echo "Hello from FAT12 file system!" | sudo tee /mnt/myos/README.TXT
echo "This is a test file" | sudo tee /mnt/myos/TEST.TXT
echo "Kernel data here" | sudo tee /mnt/myos/KERNEL.BIN

sudo umount /mnt/myos

echo "FAT12 disk image created: disk.img"