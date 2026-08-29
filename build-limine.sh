#!/bin/bash
set -e

FALLBACK_KERNEL_IMAGE="kernel-fallback.elf"
if [ -f kernel-limine.elf ]; then
  cp kernel-limine.elf "$FALLBACK_KERNEL_IMAGE"
fi

echo "Building Limine kernel (higher half, GOP)..."
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/libc/runtime.c -o purec_runtime.o
x86_64-elf-ar rcs libpurec.a purec_runtime.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/libgui/theme.c -o puregui_theme.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/libgui/draw.c -o puregui_draw.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/libgui/window.c -o puregui_window.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/libgui/event.c -o puregui_event.o
x86_64-elf-ar rcs libpuregui.a puregui_theme.o puregui_draw.o puregui_window.o puregui_event.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/libgui/widgets.c -o puregui_widgets.o
x86_64-elf-ar rcs libpguiw.a puregui_widgets.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -I./src -c src/programs/init/main.c -o init_program.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -I./src -c src/programs/installer/main.c -o installer_program.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -I./src -c src/programs/game/snake/main.c -o snake_program.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/programs/terminal/main.c -o terminal_program.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/programs/terminal/shell.c -o terminal_shell.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/programs/terminal/path.c -o terminal_path.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/programs/terminal/environment.c -o terminal_environment.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/programs/terminal/window.c -o terminal_window.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -I./src -c src/programs/nano/main.c -o nano_program.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -I./src -c src/programs/nano/editor.c -o nano_editor.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/programs/nano/window.c -o nano_window.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -I./src -c src/programs/system/main.c -o system_program.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -I./src -c src/programs/system/commands.c -o system_commands.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -I./src -c src/programs/system/filesystem.c -o system_filesystem.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -I./src -c src/programs/system/system.c -o system_platform.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/programs/gui_demo/main.c -o gui_demo_program.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/programs/files/main.c -o files_program.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/programs/files/app.c -o files_app.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/programs/files/model.c -o files_model.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/programs/files/path.c -o files_path.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=small -mno-red-zone -mgeneral-regs-only -I./src -c src/programs/files/view.c -o files_view.o
x86_64-elf-ld -T linker-userspace.ld -o init init_program.o libpurec.a
x86_64-elf-ld -T linker-userspace.ld -o installer installer_program.o libpurec.a
x86_64-elf-ld -T linker-userspace.ld -o snake snake_program.o libpurec.a
x86_64-elf-ld -T linker-userspace.ld -o terminal terminal_program.o terminal_shell.o terminal_path.o terminal_environment.o terminal_window.o libpuregui.a libpurec.a
x86_64-elf-ld -T linker-userspace.ld -o nano nano_program.o nano_editor.o nano_window.o libpuregui.a libpurec.a
x86_64-elf-ld -T linker-userspace.ld -o system system_program.o system_commands.o system_filesystem.o system_platform.o terminal_path.o libpurec.a
x86_64-elf-ld -T linker-userspace.ld -o gui-demo gui_demo_program.o libpguiw.a libpuregui.a libpurec.a
x86_64-elf-ld -T linker-userspace.ld -o files files_program.o files_app.o files_model.o files_path.o files_view.o libpguiw.a libpuregui.a libpurec.a
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/boot/boot.c -o boot_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/kernel.c -o kernel_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/init.c -o init_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/system_info.c -o system_info_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/process.c -o process_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/program_alias.c -o program_alias_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/elf.c -o elf_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/mm/pmm.c -o pmm_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/mm/vmm.c -o vmm_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/arch/x86_64/gdt.c -o gdt_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/arch/x86_64/idt.c -o idt_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/arch/x86_64/mmio.c -o mmio_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/syscall.c -o syscall_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/serial.c -o serial_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/vga.c -o vga_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/pic.c -o pic_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/timer.c -o timer_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/fb.c -o fb_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/gop.c -o gop_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/mouse/ps2_mouse.c -o ps2_mouse_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/mouse/usb_mouse.c -o usb_mouse_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/lib/string.c -o string_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/klog.c -o klog_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/boot_diag.c -o boot_diag_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/panic.c -o panic_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/keyboard.c -o keyboard_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/pci/pci.c -o pci_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/storage/ata_pio.c -o ata_pio_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/storage/ahci.c -o ahci_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/storage/block_device.c -o block_device_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/storage/storage_probe.c -o storage_probe_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/usb/xhci.c -o xhci_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/usb/ehci.c -o ehci_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/fs/fat32.c -o fat32_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/fs/vfs.c -o vfs_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/display.c -o display_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/fs.c -o userspace_fs_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/system.c -o system_api_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/userspace.c -o userspace_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/window_manager.c -o window_manager_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/monitor/monitor.c -o monitor_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/apps/datetime_service.c -o datetime_service_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/apps/clock_app.c -o clock_app_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/apps/calendar_app.c -o calendar_app_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/apps/calculator_app.c -o calculator_app_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/apps/desktop_apps.c -o desktop_apps_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/scheduler.c -o scheduler_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/power.c -o power_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/audio_hda.c -o audio_hda_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/audio.c -o audio_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/audio.c -o userspace_audio_limine.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/audio_overlay.c -o audio_overlay_limine.o
x86_64-elf-g++ -std=c++20 -g -O1 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/userspace/apps/audio_panel.cpp -o audio_panel_limine.o
nasm -f elf64 src/arch/x86_64/gdt.asm -o gdt_asm_limine.o
nasm -f elf64 src/arch/x86_64/idt.asm -o idt_asm_limine.o
nasm -f elf64 src/arch/x86_64/scheduler.asm -o scheduler_asm_limine.o
nasm -f elf64 src/arch/x86_64/user.asm -o user_asm_limine.o
x86_64-elf-ld -T linker-limine.ld -o kernel-limine.elf boot_limine.o kernel_limine.o init_limine.o system_info_limine.o process_limine.o program_alias_limine.o elf_limine.o pmm_limine.o vmm_limine.o gdt_limine.o idt_limine.o mmio_limine.o syscall_limine.o serial_limine.o vga_limine.o pic_limine.o timer_limine.o fb_limine.o gop_limine.o ps2_mouse_limine.o usb_mouse_limine.o string_limine.o klog_limine.o boot_diag_limine.o panic_limine.o keyboard_limine.o pci_limine.o ata_pio_limine.o ahci_limine.o xhci_limine.o ehci_limine.o block_device_limine.o storage_probe_limine.o fat32_limine.o vfs_limine.o display_limine.o userspace_fs_limine.o system_api_limine.o userspace_audio_limine.o audio_overlay_limine.o userspace_limine.o window_manager_limine.o monitor_limine.o datetime_service_limine.o clock_app_limine.o calendar_app_limine.o calculator_app_limine.o desktop_apps_limine.o audio_panel_limine.o scheduler_limine.o power_limine.o audio_hda_limine.o audio_limine.o gdt_asm_limine.o idt_asm_limine.o scheduler_asm_limine.o user_asm_limine.o
if [ ! -f "$FALLBACK_KERNEL_IMAGE" ]; then
  cp kernel-limine.elf "$FALLBACK_KERNEL_IMAGE"
fi
echo "kernel-limine.elf: $(file kernel-limine.elf | cut -d: -f2)"
x86_64-elf-readelf -l kernel-limine.elf | head -n12

LIMINE_SHARE=""
for p in /usr/share/limine /tmp/limine-pkg/usr/share/limine; do
  [ -d "$p" ] && LIMINE_SHARE="$p" && break
done
if [ -z "$LIMINE_SHARE" ]; then
  echo "limine не найден"
  echo "   или скачай: wget https://archlinux.org/packages/extra/x86_64/limine/download -O /tmp/limine.pkg.tar.zst && mkdir -p /tmp/limine-pkg && tar -I zstd -xf /tmp/limine.pkg.tar.zst -C /tmp/limine-pkg"
  exit 1
fi
LIMINE_BIN=""
for p in /usr/bin/limine /tmp/limine-pkg/usr/bin/limine; do
  [ -f "$p" ] && LIMINE_BIN="$p" && break
done

echo "Limine: $LIMINE_SHARE"
rm -rf iso_limine
mkdir -p iso_limine/boot/limine iso_limine/EFI/BOOT iso_limine/bin/program iso_limine/lib iso_limine/include

cp kernel-limine.elf iso_limine/boot/kernel.elf
cp "$FALLBACK_KERNEL_IMAGE" iso_limine/boot/kernel-fallback.elf
cp init iso_limine/bin/init
cp installer iso_limine/bin/installer
cp snake iso_limine/bin/snake
cp terminal iso_limine/bin/program/terminal
cp nano iso_limine/bin/program/nano
cp system iso_limine/bin/program/system
cp files iso_limine/bin/program/files
cp gui-demo iso_limine/bin/gui-demo
cp libpurec.a iso_limine/lib/libpurec.a
cp libpuregui.a iso_limine/lib/libpuregui.a
cp libpguiw.a iso_limine/lib/libpguiw.a
cp src/libgui/include/puregui.h iso_limine/include/puregui.h
cp src/libgui/include/pguiw.h iso_limine/include/pguiw.h
cat > iso_limine/boot/limine/limine.conf <<'EOF'
timeout: 10
verbose: yes
serial: yes
/PureC OS 64-bit Limine+GOP (primary)
    protocol: limine
    kernel_path: boot():/boot/kernel.elf
    module_path: boot():/boot/kernel-fallback.elf
    module_path: boot():/EFI/BOOT/BOOTX64.EFI
    module_path: boot():/bin/init
    module_path: boot():/bin/installer
    module_path: boot():/bin/snake
    module_path: boot():/bin/program/terminal
    module_path: boot():/bin/program/nano
    module_path: boot():/bin/program/system
    module_path: boot():/bin/program/files
    module_path: boot():/bin/gui-demo
    module_path: boot():/lib/libpurec.a
    module_path: boot():/lib/libpuregui.a
    module_path: boot():/lib/libpguiw.a
    module_path: boot():/include/puregui.h
    module_path: boot():/include/pguiw.h
/PureC OS 64-bit (fallback previous image)
    protocol: limine
    kernel_path: boot():/boot/kernel-fallback.elf
    module_path: boot():/EFI/BOOT/BOOTX64.EFI
    module_path: boot():/bin/init
    module_path: boot():/bin/installer
    module_path: boot():/bin/snake
    module_path: boot():/bin/program/terminal
    module_path: boot():/bin/program/nano
    module_path: boot():/bin/program/system
    module_path: boot():/bin/program/files
    module_path: boot():/bin/gui-demo
    module_path: boot():/lib/libpurec.a
    module_path: boot():/lib/libpuregui.a
    module_path: boot():/lib/libpguiw.a
    module_path: boot():/include/puregui.h
    module_path: boot():/include/pguiw.h
EOF
cp iso_limine/boot/limine/limine.conf iso_limine/limine.conf

cp "$LIMINE_SHARE/limine-bios.sys" iso_limine/boot/limine/ 2>/dev/null || true
cp "$LIMINE_SHARE/limine-bios-cd.bin" iso_limine/boot/limine/ 2>/dev/null || true
cp "$LIMINE_SHARE/limine-uefi-cd.bin" iso_limine/boot/limine/ 2>/dev/null || true
cp "$LIMINE_SHARE/BOOTX64.EFI" iso_limine/EFI/BOOT/ 2>/dev/null || true
cp "$LIMINE_SHARE/BOOTIA32.EFI" iso_limine/EFI/BOOT/ 2>/dev/null || true
cp "$LIMINE_SHARE/limine-bios.sys" iso_limine/ 2>/dev/null || true
cp "$LIMINE_SHARE/limine-bios.sys" iso_limine/boot/ 2>/dev/null || true

echo "xorriso (BIOS+UEFI)..."
xorriso -as mkisofs \
  -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
  --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label \
  iso_limine -o purec_limine.iso 2>&1 | tail -5

echo "limine bios-install..."
chmod +x "$LIMINE_BIN" 2>/dev/null || true
"$LIMINE_BIN" bios-install purec_limine.iso 2>&1 | tail -5

ls -lh purec_limine.iso kernel-limine.elf init installer snake terminal nano system files gui-demo libpurec.a libpuregui.a libpguiw.a
echo "purec_limine.iso"
