# PureC OS Reverse Specification

## Scope and Stack
This specification covers Limine x86_64 boot, memory, scheduling, process,
system-call and filesystem boundaries. The implementation is freestanding C,
C++20 for selected desktop modules, NASM assembly and custom linker scripts.

## Observed Architecture
- `src/boot/boot.c` captures Limine framebuffer, HHDM, memory-map, kernel-file,
  module, firmware, ACPI and SMP responses.
- `src/mm/pmm.*` allocates 4 KiB frames from usable Limine memory.
- `src/mm/vmm.*` owns user page-table creation, mapping, validation and release.
- `src/kernel/process.*` owns PID, ELF process lifecycle and descriptor tables.
- `src/kernel/scheduler.*` preempts kernel and user threads on the PIT tick.
- `src/kernel/syscall.*` is the ring-3 boundary and validates user memory.
- `src/fs/vfs.*` routes virtual kernel files and the FAT32 root backend.
- `src/libc/*` builds the common freestanding `libpurec.a` application API.
- `src/programs/init/main.c` is the mandatory PID-1 image.
- `src/programs/installer/main.c` is a graphical ring-3 installation program.
- `src/programs/game/snake/main.c` is the standalone graphical Snake image.

## Observed Requirements
- OBS-BOOT-001: When Limine omits HHDM or the memory map, the kernel shall panic
  before initializing process memory (`src/boot/boot.c`, `src/kernel/kernel.c`).
- OBS-MEM-001: When a user address space is created, the kernel shall copy only
  higher-half PML4 entries and map ELF pages as user-accessible (`src/mm/vmm.c`).
- OBS-PROC-001: When `SYS_EXEC` names a valid boot-module ELF64 image, the kernel
  shall create a PID, address space, user stack and schedulable thread
  (`src/kernel/process.c`, `src/kernel/elf.c`).
- OBS-PROC-002: When a user process faults, the kernel shall terminate that
  process instead of invoking the global panic path (`src/arch/x86_64/idt.c`).
- OBS-PROC-003: When `SYS_WAIT` observes an exited process, the kernel shall
  return its status and release its user mappings (`src/kernel/process.c`).
- OBS-FD-001: While a process is active, VFS descriptors shall be resolved
  through its private descriptor table (`src/kernel/process.c`).
- OBS-SEC-001: When a syscall receives a user pointer, the kernel shall reject
  pages that are absent, supervisor-only, or read-only for output
  (`src/kernel/syscall.c`, `src/mm/vmm.c`).
- OBS-SEC-002: When a process without storage-administration permission requests
  disk formatting, the syscall shall fail (`src/kernel/syscall.c`).
- OBS-INSTALL-001: When the ISO is assembled, the installer shall be copied as
  `/bin/installer` and shall not be linked into the kernel ELF
  (`build-limine.sh`).
- OBS-INIT-001: When userspace starts, `/bin/init` shall be created before every
  other process and shall have PID 1 (`src/kernel/init.c`).
- OBS-INSTALL-002: While disk installation runs, the GUI shall obtain progress
  from completed FAT32 stages rather than elapsed time (`src/fs/fat32.c`).

## Non-Functional Observations
- Scheduling is single-core even when Limine reports additional CPUs.
- PMM capacity is deliberately bounded to 32 GiB.
- VFS operations are serialized with one yielding spin lock.
- User programs use interrupt `0x80`; no `syscall/sysret` fast path exists.

## Inferred Acceptance Criteria
- Kernel and installer link outputs contain disjoint installer implementation.
- Starting `install` returns a positive PID and blocks the shell in `SYS_WAIT`.
- Installer keyboard, display, storage and FAT operations cross `int 0x80`.
- Exiting or faulting installer returns control to the desktop terminal.
- A non-installer ELF receives an error from every format syscall.

## Uncertainties and Follow-up
- AP startup, per-CPU scheduler state and TSS instances are not implemented.
- There is no IPC ABI yet, so FAT32 cannot honestly run as an isolated server.
- There is no demand paging, copy-on-write, signal delivery or process groups.
- Runtime verification is intentionally absent because repository instructions
  prohibit builds and tests for this change.
