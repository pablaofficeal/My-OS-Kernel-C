# ADR-009: PureGUI focus and coordinated repaint

## Status

Accepted

## Context

The desktop launcher synchronously waited for every external program and used
a single `external_program_active` flag to stop desktop mouse and keyboard
handling. Consequently an open terminal or file manager prevented launching a
second application.

Allowing processes to run without that flag is insufficient: every PureGUI
client polls the same mouse and keyboard queues, and direct framebuffer redraws
can erase windows belonging to other processes.

The file manager also rendered its complete view for every mouse-move event,
even when no hover or application state changed.

## Decision

Add a bounded system window registry for PureGUI processes. Each window
registers its frame, the desktop raises the topmost window under a click, and
only the focused process may consume keyboard input. Normal desktop launches
are detached and reaped asynchronously; the installer retains its supervised
exclusive launch path.

The desktop input and keyboard loops sleep after each polling pass. This is
required by the strict-priority scheduler: a continuously ready priority-zero
desktop thread would otherwise starve detached ring-3 processes before their
first instruction.

When the desktop surface must be restored, registered windows receive ordered
`PG_EVENT_REPAINT` events from bottom to top. `pg_window_end()` acknowledges the
completed repaint before the next window is allowed to render. A bounded wait
prevents a client blocked in a child process from freezing the desktop.

PureGUI API version 3 also exposes `PG_EVENT_FOCUS`. The file manager ignores
plain `PG_EVENT_MOUSE_MOVE` events and redraws only for clicks, keyboard state,
focus, explicit repaint, minimize or actual window movement.

## Consequences

### Positive

- A terminal and file manager can remain open at the same time.
- Desktop icons remain interactive outside managed windows.
- Keyboard characters have one consumer instead of racing between processes.
- Window restoration follows z-order without repainting on passive pointer
  movement.
- Exited detached programs are reaped and do not permanently consume process
  table slots.
- Detached programs receive CPU time while the desktop remains interactive.

### Negative

- The registry is bounded to eight PureGUI windows.
- Rendering remains direct-to-framebuffer and is less efficient than surface
  composition.
- A client that does not service events can miss a restoration after the
  bounded repaint timeout.

### Neutral

- The installer remains exclusive because its crash supervision and disk
  workflow intentionally block ordinary desktop interaction.

## Alternatives Considered

**Only remove `external_program_active`** was rejected because it creates
keyboard races and simultaneous window dragging.

**Redraw every window on every mouse movement** was rejected because it caused
the visible flicker and unnecessary full-window rendering reported here.

**Introduce off-screen compositor surfaces immediately** was deferred; the
registry defines focus and repaint contracts that a compositor can later use.

## References

- `src/userspace/window_manager.c`
- `src/libgui/event.c`
- `src/userspace/userspace.c`
- `docs/adr-007-puregui-desktop-redraw-contract.md`
