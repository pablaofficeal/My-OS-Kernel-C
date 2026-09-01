# Atheros AR928X (AR9280/AR9285/AR9287) — драйвер для My-OS-Kernel-C

## Статус: ЧЕСТНО НЕ ТЕСТИРОВАЛСЯ

Драйвер **не тестировался на реальном железе и не эмулировался**. У меня нет возможности сейчас эмулировать AR928X:

- QEMU не эмулирует AR928X (поддерживает только e1000/rtl8139/virtio, ath9k требует отдельной модели).
- Реальной карты AR9280/9285/9287 в наличии нет.
- Код компилируется (`make -C src/drivers/net/ar928x` → `bin/modules/ar928x.ko`, `make -C src/kernel` → `kernel-limine.elf` с `ar928x_module_init`), но запуск в железе/QEMU с пробросом PCI не выполнялся.

Поэтому драйвер считается **compile-tested, not run-tested**. Все заявления о работе — только на основе сверки с `linux/drivers/net/wireless/ath/ath9k`.

## Структура

```
src/drivers/net/ar928x/
  Makefile                 — Kbuild-подобный, ld -r → ar928x.ko + ar928x.a
  include/                 — ВСЕ заголовки в одном месте (8 шт)
    ar928x.h               — struct ar928x_device, struct ar928x_desc
    ar928x_reg.h           — AR928X_REG_SREV 0x4020, CR, ISR, STA_ID0
    ar928x_pci.h
    ar928x_hw.h
    ar928x_eeprom.h
    ar928x_mac.h
    ar928x_dma.h
    ar928x_wifi.h
  core/ar928x_main.c       — net_device_ops, ar928x_module_init()
  pci/ar928x_pci.c         — probe 0x168c:0029/002A/002B/002D/002E, BAR, PM D0
  hw/ar928x_hw.c           — SREV, reset, disable interrupts
  hw/ar928x_eeprom.c       — EEPROM 0x2000/0x401C → MAC
  hw/ar928x_mac.c          — STA_ID0 0x8000
  dma/ar928x_dma.c         — кольца 16*32B + 16*4K
  wifi/ar928x_wifi.c       — wifi_ops scan/connect/poll (soft-scan 1200мс)
```

## Сборка как отдельный модуль

```sh
make modules          # собирает src/drivers/net/ar928x → bin/modules/ar928x.ko
make kernel           # линкует --whole-archive bin/modules/ar928x.a в kernel-limine.elf
make iso              # копирует bin/modules/ar928x.ko → iso_root/bin/modules/
```

Limine: `src/boot/limine.conf` содержит `module_path: boot():/bin/modules/ar928x.ko`.
Загрузчик: `src/kernel/module.c` — ELF ET_REL, R_X86_64_64/PC32, таблица `g_ksyms` (pci_enumerate, mmio_map, klogf, net_device_register ...). Fallback — built-in через `ar928x_module_init` weak.

## Инициализация

`ar928x_module_init()` в `core/ar928x_main.c:14`:
`pci_probe → pci_enable → pci_ensure_power → mmio_map → srev → disable_interrupts → hw_reset → mac_setup → dma_allocate → dma_init_hw → net_device_register(wlan1) → wifi_device_register → wifi_trigger_scan`

`net_service.c:39` вызывает `modules_init()` после `e1000`/`ax201`, помечает `ready` если `net_device_count()>0`.

## Что работает без железа

- PCI probe логирует `ar928x: no Atheros AR928X hardware found` и корректно возвращает false.
- При наличии QEMU без карты — `wlan1` не создается, система продолжает работать на e1000.
- При наличии карты — soft-scan инжектит `AR928X-Demo` если кэш пуст, `wifi_notify_scan_done` через 1200мс.

## Что НЕ готово / требует теста

- Заливка initvals `ar5008_initvals.h` / `ar9002_initvals.h` (BB/Radio) — сейчас `HAL deferred`.
- Калибровки IQ/ADC/NF, QCU/DCU, RXDP HP/LP, TXDP Q0..Q9, прерывания `AR_IMR`.
- RX path: парсинг 802.11 mpdu → `net_device_receive` → `wifi_report_scan_result` для реальных beacons.
- TX path: программирование `AR_Q0_TXDP 0x0800` и kick.
- Проверка `SREV` для AR9287 (версия 0x180 в 8-бит truncation), EEPROM layout для разных ревизий.
- Тест `connect`/`disconnect` с реальным AP (сейчас timeout 2500мс → `wifi_notify_connect_failed`).

## Ограничения честной документации

Автор не скрывает, что драйвер не прошел:
- ни одного теста на `qemu -device ar928x` (такого устройства нет),
- ни одного теста с PCI passthrough реальной карты,
- ни одного теста `SYS_WIFI_SCAN`/`SYS_WIFI_LIST` на целевом железе.

Любой отчет об успешном сканировании/подключении требует повторной проверки на реальном AR928X.

## Как проверить когда появится возможность

1. `lspci -nn | grep 168c` должен показать `002a` или `002b`.
2. Загрузить ISO, в логе `ar928x: hardware detected: Atheros AR9280 ... at 02:00.0`, `SREV raw=0x...`, `MAC ...`, `DMA rings`, `wlan1 ready`.
3. `settings → Wi-Fi` должен показать scan, `dmesg` — `soft-scan` или реальные beacons.
4. При проблемах — `EC [D9]`, `GP_CNTRL`, `SREV=0xFFFFFFFF` указывают на power gated/RFKILL.

## Лицензия

Как у ядра — без отдельного заголовка, код без комментариев по требованию.
