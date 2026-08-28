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

User pointers are validated against the current process page tables. A pointer
must reference present ring-3 pages and output buffers must also be writable.

## Processes

- `SYS_GETPID`: returns the current PID; kernel threads receive PID 0.
- `SYS_EXEC`: starts a trusted ELF64 Limine module by path and returns its PID;
  `a2` optionally points to a command-line string copied into the child.
- `SYS_WAIT`: waits for a PID and optionally writes its exit status.
- `SYS_EXIT`: terminates only the calling process or kernel thread.
- `SYS_SCHED_YIELD`: voluntarily gives up the current time slice.
- `SYS_SLEEP`: blocks the calling thread until its timer deadline.
- `SYS_GETCHAR`: returns one keyboard character to a foreground program.
- `SYS_TRY_GETCHAR`: returns a character or `-1` without blocking.
- `SYS_GET_COMMAND_LINE`: copies the current process command line to `a1`;
  `a2` is the destination capacity.
- `SYS_GET_PROCESS_NAME`: copies the executable alias used for the current
  process. The shared system-program image uses it for multicall dispatch.
- `SYS_ENV_GET`, `SYS_ENV_SET`, `SYS_ENV_UNSET`: access the current process
  environment. Names are limited to 31 characters and values to 127.
- `SYS_ENV_LIST`: copies a bounded list of environment entries. A child gets a
  private copy of its parent's environment during `SYS_EXEC`.
- `SYS_INSTALL_START`: starts the privileged asynchronous UEFI install worker.
- `SYS_INSTALL_STATUS`: returns the real install stage, progress and result.
- `SYS_INSTALL_LOG`: returns the bounded history of install stages and their
  progress percentages so a restarted GUI can reconstruct the live log.

ELF programs run in ring 3 with a private CR3 and user stack. The kernel half
of the address space remains supervisor-only. The installer module receives
the storage-administration capability; ordinary processes cannot format disks.

`/bin/init` is mandatory and must receive PID 1. Boot-media applications are
addressed as `/bin/installer`, `/bin/snake`, `/bin/program/terminal` and
`/bin/program/nano`. The minimal shell does not search `PATH`; executable names
must be absolute.

Traditional commands such as `/bin/program/ls`, `/bin/program/cat` and
`/bin/program/install` are aliases of the trusted `/bin/program/system` module.
They remain separate executable paths while sharing one ring-3 implementation.

## File Descriptors
VFS descriptors follow the Linux shape:
- `0`: stdin
- `1`: stdout
- `2`: stderr
- `3+`: files opened by `SYS_OPEN`

Descriptors are translated through the calling process table. Programs must
close descriptors with `SYS_CLOSE`. `SYS_READ` returns `0` at
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

## Audio

- `SYS_AUDIO_GET_STATUS`: `a1` points to `struct audio_status`.
- `SYS_AUDIO_GET_VOLUME`: returns logical volume from `0` to `100`.
- `SYS_AUDIO_SET_VOLUME`: `a1` is logical volume from `0` to `100`.
- `SYS_AUDIO_IS_MUTED`: returns `1` when muted, otherwise `0`.
- `SYS_AUDIO_SET_MUTED`: `a1 != 0` mutes audio.
- `SYS_AUDIO_ADJUST_VOLUME`: `a1` is a signed step.
- `SYS_AUDIO_PLAY_TEST_SOUND`: plays the current backend's test sound.
- `SYS_AUDIO_UPDATE`: advances non-blocking audio state from the scheduler loop.

## Kernel Virtual Files
The kernel exposes a small read-only virtual filesystem:
- `/kernel/version`
- `/kernel/init`
- `/kernel/abi`

These files are owned by the kernel and are not stored on FAT32.
