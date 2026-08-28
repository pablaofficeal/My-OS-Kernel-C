# PureC Syscall ABI

## Calling Convention
System calls use `int 0x80`.

Registers:
- `rax`: syscall number
- `rbx`: argument 1
- `rcx`: argument 2
- `rdx`: argument 3
- `rsi`: argument 4
- `rdi`: argument 5

Return values:
- `>= 0`: success
- `< 0`: `FS_ERROR_*` or subsystem error code

## File Descriptors
VFS descriptors follow the Linux shape:
- `0`: stdin
- `1`: stdout
- `2`: stderr
- `3+`: files opened by `SYS_OPEN`

Programs must close descriptors with `SYS_CLOSE`. `SYS_READ` returns `0` at
EOF and does not close the descriptor.

## Stable VFS Calls
- `SYS_OPEN` / `SYS_FILE_OPEN`
- `SYS_READ` / `SYS_FILE_READ`
- `SYS_CLOSE` / `SYS_FILE_CLOSE`
- `SYS_UNLINK` / `SYS_FILE_DELETE`
- `SYS_RENAME` / `SYS_FILE_RENAME`
- `SYS_MKDIR` / `SYS_DIR_CREATE`
- `SYS_DIR_LIST`
- `SYS_FILE_CREATE`
- `SYS_FILE_WRITE`
- `SYS_FILE_MOVE`

## Kernel Virtual Files
The kernel exposes a small read-only virtual filesystem:
- `/kernel/version`
- `/kernel/init`
- `/kernel/abi`

These files are owned by the kernel and are not stored on FAT32.
