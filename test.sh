# Сначала загрузчик (ровно 512 байт)
cat boot.bin > os-image.bin
# Потом наше ядро
cat kernel.bin >> os-image.bin
# Заполним образ до размера 1.44 МБ (для дискеты)
dd if=/dev/zero bs=512 count=2879 >> os-image.bin 2>/dev/null