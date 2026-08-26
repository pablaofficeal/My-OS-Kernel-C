#!/bin/bash
set -e
# build.sh - PureC OS 64-bit Long Mode + UEFI/BIOS (GRUB) + GDT/IDT
# Микс-структура: src/boot | src/arch/x86_64 | src/drivers | src/kernel | src/lib

echo "🧹 Cleaning..."
rm -f *.o *.elf *.iso
rm -rf iso
mkdir -p iso/boot/grub

echo "🔨 Building 64-bit kernel (x86_64-elf, -mcmodel=kernel)..."
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/boot/grub_main.c -o grub_main.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/arch/x86_64/gdt.c -o gdt.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/arch/x86_64/idt.c -o idt.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/serial.c -o serial.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/fb.c -o fb.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/lib/string.c -o string.o
nasm -f elf64 src/arch/x86_64/gdt.asm -o gdt_asm.o
nasm -f elf64 src/arch/x86_64/idt.asm -o idt_asm.o
nasm -f elf64 src/boot/multiboot_header.asm -o multiboot.o

echo "🔗 Linking (ENTRY=multiboot_entry, base 0x100000)..."
cat > /tmp/linker_grub.ld <<'LD'
ENTRY(multiboot_entry)
SECTIONS { . = 0x100000; .multiboot : { KEEP(*(.multiboot)) } .requests : { KEEP(*(.requests*)) } .text : { *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(COMMON) *(.bss*) } /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) } }
LD
x86_64-elf-ld -T /tmp/linker_grub.ld -o kernel.elf multiboot.o grub_main.o gdt.o idt.o serial.o fb.o string.o gdt_asm.o idt_asm.o
echo "✅ kernel.elf: $(file kernel.elf | cut -d: -f2)"
x86_64-elf-readelf -l kernel.elf | head -n12

echo "📦 ISO layout (GRUB Hybrid BIOS+UEFI)..."
cp kernel.elf iso/boot/kernel.elf
cat > iso/boot/grub/grub.cfg <<'EOF'
set timeout=0
set default=0
terminal_output console
menuentry "PureC OS 64-bit" {
    multiboot2 /boot/kernel.elf
    boot
}
EOF
grub-mkrescue -o purec_os.iso iso/ 2>&1 | tail -5
echo "✅ purec_os.iso $(du -h purec_os.iso | cut -f1)"
ls -lh kernel.elf purec_os.iso

echo "🚀 BIOS:  qemu-system-x86_64 -cdrom purec_os.iso -m 512M -nographic -serial stdio"
echo "🚀 UEFI:  qemu-system-x86_64 -bios /usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd -drive if=pflash,format=raw,file=/tmp/ovmf_vars.fd -cdrom purec_os.iso -m 512M -nographic -serial stdio"
echo "   Serial log proves GDT/IDT: expect [GDT] loaded, [IDT] #BP caught"
# Keep Limine sources for pure UEFI (src/boot/boot.c) - build via Limine if needed
