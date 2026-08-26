#!/bin/bash
set -e
echo "🧹 Cleaning..."
rm -f *.o *.elf *.iso
rm -rf iso
mkdir -p iso/boot/grub

echo "🔨 Building Hybrid 64-bit kernel (Limine+GOP + GRUB+VGA + GDT/IDT + Syscalls)..."
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/boot/boot.c -o boot.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/boot/grub_main.c -o grub_main.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/kernel.c -o kernel.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/arch/x86_64/gdt.c -o gdt.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/arch/x86_64/idt.c -o idt.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/kernel/syscall.c -o syscall.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/serial.c -o serial.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/vga.c -o vga.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/pic.c -o pic.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/fb.c -o fb.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/drivers/gop.c -o gop.o
x86_64-elf-gcc -g -O1 -ffreestanding -fno-stack-protector -fno-pic -m64 -mcmodel=kernel -mgeneral-regs-only -mno-red-zone -I./src -c src/lib/string.c -o string.o
nasm -f elf64 src/arch/x86_64/gdt.asm -o gdt_asm.o
nasm -f elf64 src/arch/x86_64/idt.asm -o idt_asm.o
nasm -f elf64 src/boot/multiboot_header.asm -o multiboot.o

echo "🔗 Linking (ENTRY=_start, hybrid Limine+Multiboot2 @0x100000)..."
x86_64-elf-ld -T linker.ld -o kernel.elf boot.o multiboot.o grub_main.o kernel.o gdt.o idt.o syscall.o serial.o vga.o pic.o fb.o gop.o string.o gdt_asm.o idt_asm.o
echo "✅ kernel.elf: $(file kernel.elf | cut -d: -f2)"
x86_64-elf-readelf -l kernel.elf | head -n12

echo "📦 ISO (GRUB Hybrid BIOS+UEFI, GOP via Limine, VGA via BIOS)..."
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
echo "🚀 BIOS QEMU: qemu-system-x86_64 -cdrom purec_os.iso -m 512M -nographic"
echo "   VGA видно в графике, serial в -nographic"
echo "   Тест: GDT/IDT int3 + GOP (если Limine UEFI) + syscalls int0x80"
