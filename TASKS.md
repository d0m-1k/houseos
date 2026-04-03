# HouseOS Unix-like Task List

## 1. Kernel And Syscall Semantics
- [x] Normalize syscall error behavior and stable return conventions.
- [x] Finalize process lifecycle semantics: spawn/exec/exit/task_state.
- [x] Add robust child wait semantics without hangs on short-lived processes.
- [x] Unify fd lifecycle behavior across open/read/write/close and edge cases.

## 2. VFS Compatibility
- [x] Normalize path resolution behavior (`.`, `..`, absolute/relative, trailing slash).
- [x] Harden link/unlink/rmdir semantics and directory constraints.
- [x] Expand file metadata behavior consistency.
- [x] Stabilize mount/umount behavior and failure cases.

## 3. procfs/devfs/pty
- [x] Keep `/proc/version` as OS version string contract.
- [x] Keep `/proc/self/*` alias behavior and process-relative path parity.
- [x] Improve `/proc/<pid>/fd/*` and directory/listing semantics.
- [x] Tighten tty/pty foreground and terminal interaction behavior.

## 4. Shell And Core Userland
- [x] Harden shell execution semantics and exit-status behavior.
- [x] Keep `cmd` as multi-call binary (argv[0]-based applet dispatch).
- [x] Keep `cmd install` self-resolution via `/proc/self/exe` with fallback.
- [x] Expand and align essential applet behavior with Unix-like expectations.

## 5. IPC And Networking
- [x] Stabilize socket syscall behavior for existing UDP stack.
- [x] Expand FIFO/socket file behavior consistency in VFS/userspace.

## 6. Regression And Runtime Validation
- [x] Add focused regression checks per changed subsystem.
- [x] Validate runtime in QEMU with serial stdio.
- [x] Validate final image on canonical `build/system.img`.
- [x] Keep temporary runtime artifacts cleaned up after tests.

## 7. Documentation
- [x] Keep `README.md` and `README.RU.md` aligned with behavior and workflow.
- [x] Maintain a compatibility/status section for implemented Unix-like behavior.

## Current Execution Batch (2026-04-03)
- [x] Remove comments from `Makefile` and `.gitignore`.
- [x] Remove `FORCE`, `restart-os`, `rebuild-restart`, `vnc-shot` from `Makefile`.
- [x] Improve procfs directory compatibility for trailing slash in pid/fd paths.
- [x] Add and run unix-like regression script.
- [x] Add and run QEMU serial smoke validation script.
- [x] Fix `SYS_GET_TICKS` behavior.
- [x] Tighten vfs unlink/rmdir and nested umount guards.
- [x] Add socket timeout/nonblocking behavior in syscall UDP path.
- [x] Validate tty foreground pid ownership checks.
- [x] Add errno-style syscall error mapping in kernel/userspace wrappers.
- [x] Add kernel/userspace `waitpid` support and shell child reaping path.
- [x] Fit initramfs payload to boot-time low-memory preload limits.
