# ADR-004: PID 1, Userspace Binary Layout and GUI Installer

## Status
Accepted

## Context
The first process was whichever program happened to be started from the shell,
the installer had a text interface, and Snake still had a linked kernel object.
Applications duplicated raw `int 0x80` wrappers. A synchronous format syscall
also prevented an installer process from drawing truthful progress updates.

## Decision
Require the kernel startup path to load `/bin/init` first and reject boot when
it does not receive PID 1. Keep the existing linked desktop temporarily, but
launch standalone applications from it as foreground ring-3 processes.

Place executable boot modules under `/bin`: `init`, `installer`, and `snake`.
During installation copy them to the disk, additionally placing Snake at
`/game/snake`. Place the development library at `/lib/libpurec.a`.

Build `libpurec.a` as a freestanding static userspace standard library. It owns
the syscall calling convention, basic strings and numbers, process functions,
file probing, input, mouse and framebuffer primitives. Static linking is used
because the current ELF loader has no dynamic relocation support.

Expose the installer as a desktop icon only while `/purec/install.cfg` is
absent. Its GUI lists real block devices and selects them with the mouse. Disk
formatting runs in a kernel worker and reports real FAT32 stages through
`SYS_INSTALL_STATUS`; the ring-3 GUI polls this state and redraws its progress
bar without blocking.

## Consequences

### Positive
- PID 1 has deterministic init semantics.
- Installer and Snake faults cannot corrupt the kernel address space.
- All new programs share one public userspace API.
- Progress reflects completed GPT, ESP, boot-payload and system-volume stages.
- Installed systems do not keep advertising the installer on the desktop.

### Negative
- `libpurec.a` is copied into each executable at link time; shared libraries
  require a future dynamic loader.
- The desktop itself is still linked ring-0 code.
- The install worker is single-instance and formatting remains globally
  serialized.

### Neutral
- The ISO and installed ESP contain the same `/bin` payload.
- `/game/snake` is an installed alias while boot-time execution uses the trusted
  `/bin/snake` module.

## Alternatives Considered
- A fake animated progress bar was rejected because it would not represent disk
  state.
- A synchronous formatting syscall was rejected because the calling GUI cannot
  render while blocked.
- A dynamic `.so` was deferred because `PT_DYNAMIC` and relocations are not
  implemented.

## Failure Modes
- Missing `/bin/init` stops boot before the scheduler starts.
- Missing program or library modules make installation fail before reporting
  success.
- Worker failure is returned with a terminal progress state and error code.
- A missing install marker keeps the desktop installer icon visible.
