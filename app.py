import os
import subprocess
import shutil
import glob

def run_command(command, error_message="Command failed"):
    """Execute a shell command and handle errors."""
    result = subprocess.run(command, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"{error_message}: {result.stderr}")
        exit(1)

def clean():
    """Clean up directories and files."""
    if os.path.exists("iso"):
        shutil.rmtree("iso")
    for ext in ["*.o", "*.bin", "*.iso"]:
        for file in glob.glob(ext):
            os.remove(file)

def compile_file(src, out):
    """Compile a single C file with GCC."""
    cmd = (
        f"gcc -m32 -ffreestanding -fno-pie -nostdlib -fno-stack-protector -O1 "
        f"-I./src -c {src} -o {out}"
    )
    run_command(cmd, f"❌ Compilation failed for {src}")

def main():
    # Clean up
    clean()

    # List of source and output files
    files = [
        ("src/start.c", "start.o"),
        ("src/boot/lowlevel.c", "lowlevel.o"),
        ("src/kernel.c", "kernel.o"),
        ("src/drivers/screen.c", "screen.o"),
        ("src/drivers/text_output.c", "text_output.o"),
        ("src/drivers/keyboard/keyboard.c", "keyboard.o"),
        ("src/lib/string.c", "string.o"),
        ("src/lib/memory.c", "memory.o"),
        ("src/lib/error_handler.c", "error_handler.o"),
        ("src/shell/shell.c", "shell.o"),
        ("src/shell/commands.c", "commands.o"),
        ("src/fs/disk.c", "disk.o"),
        ("src/fs/fat16.c", "fat16.o"),
        ("src/tools/hexedit.c", "hexedit.o"),
        ("src/game/snake/snake.c", "snake.o"),
        ("src/game/tetris/tetris.c", "tetris.o"),
        ("src/drivers/pci/pci.c", "pci.o"),
        ("src/drivers/wifi/wifi.c", "wifi.o"),
        ("src/drivers/wifi/intel_ax210.c", "ax210.o"),
        ("src/drivers/usb/usb_driver.c", "usb_driver.o"),
    ]

    # Compile all C files
    for src, out in files:
        compile_file(src, out)
    
    # Compile assembly files
    run_command("nasm -f elf32 src/boot/lowlevel.asm -o lowlevel_asm.o", "❌ Failed to compile lowlevel.asm")
    run_command("nasm -f elf32 src/drivers/mouse/mouse.asm -o mouse.o", "❌ Failed to compile mouse.asm")

    # Link object files
    base_objects = " ".join(out for _, out in files)
    all_objects = f"{base_objects} lowlevel_asm.o mouse.o"
    cmd = f"ld -m elf_i386 -T linker.ld -o kernel.elf {all_objects}"
    run_command(cmd, "❌ Linking failed! Check for errors above.")
    # Создаем kernel.bin для совместимости
    run_command("objcopy -O binary kernel.elf kernel.bin", "❌ objcopy failed")

    # Verify kernel.elf exists
    if not os.path.exists("kernel.elf"):
        print("❌ Kernel ELF not found!")
        exit(1)

    # Create ISO directory structure
    os.makedirs("iso/boot/grub", exist_ok=True)
    shutil.copy("kernel.elf", "iso/boot/kernel.elf")

    # Create GRUB configuration
    grub_cfg = """
set timeout=5
set default=0

menuentry "PureC OS (Multiboot)" {
    echo "Loading PureC OS kernel with Multiboot..."
    multiboot /boot/kernel.elf
    boot
}
"""
    with open("iso/boot/grub/grub.cfg", "w") as f:
        f.write(grub_cfg)

    # Create ISO
    run_command("grub-mkrescue -o myos.iso iso/ 2>/dev/null", "❌ ISO creation failed!")

    # Verify ISO exists
    if not os.path.exists("myos.iso"):
        print("❌ ISO file not found!")
        exit(1)

    # Clean up object files
    for _, out in files:
        if os.path.exists(out):
            os.remove(out)

    # Run QEMU
    run_command("qemu-system-i386 -cdrom myos.iso -m 512M", "❌ QEMU failed to run!")

if __name__ == "__main__":
    main()