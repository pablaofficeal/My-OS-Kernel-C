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
        ("src/kernel.c", "kernel.o"),
        ("src/drivers/screen.c", "screen.o"),
        ("src/drivers/keyboard.c", "keyboard.o"),
        ("src/lib/string.c", "string.o"),
        ("src/shell/shell.c", "shell.o"),
        ("src/shell/commands.c", "commands.o"),
        ("src/fs/disk.c", "disk.o"),
        ("src/fs/fat16.c", "fat16.o"),
        ("src/tools/hexedit.c", "hexedit.o"),
        ("src/game/snake/snake.c", "snake.o"),
        ("src/game/tetris/tetris.c", "tetris.o"),
        ("src/game/2048/game_common.c", "game_common.o"),
        ("src/game/2048/field_4x4.c", "field_4x4.o"),
        ("src/game/2048/field_8x8.c", "field_8x8.o"),
        ("src/game/2048/field_16x16.c", "field_16x16.o"),
        ("src/game/2048/game_start.c", "game_start.o"),
        ("src/drivers/pci/pci.c", "pci.o"),
        ("src/drivers/wifi/wifi.c", "wifi.o"),
        ("src/drivers/wifi/intel_ax210.c", "ax210.o"),
    ]

    # Compile all files
    for src, out in files:
        compile_file(src, out)

    # Link object files
    objects = " ".join(out for _, out in files)
    cmd = f"ld -m elf_i386 -T linker.ld -o kernel.bin {objects}"
    run_command(cmd, "❌ Linking failed! Check for errors above.")

    # Verify kernel.bin exists
    if not os.path.exists("kernel.bin"):
        print("❌ Kernel binary not found!")
        exit(1)

    # Create ISO directory structure
    os.makedirs("iso/boot/grub", exist_ok=True)
    shutil.copy("kernel.bin", "iso/boot/")

    # Create GRUB configuration
    grub_cfg = """
set timeout=5
set default=0

menuentry "PureC OS - Hex Editor & Games" {
    multiboot /boot/kernel.bin
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