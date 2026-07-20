# NetChecker GUI (GTK3)

A GTK3 GUI conversion of the original console NetChecker app. All original
functions and logic are preserved — device add/edit/delete/search, the
background ping-monitor thread, CSV persistence, user management, and the
login/registration flow — with GTK dialogs and tree views replacing the
console menu.

## Build on Windows (MSYS2)

1. Install [MSYS2](https://www.msys2.org/).
2. Open the **MSYS2 MinGW64** shell (not the plain MSYS2 shell) and run:
   ```
   pacman -Syu
   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-gtk3 make
   ```
3. From the folder with `netchecker_gui.c` and `Makefile`:
   ```
   make
   ```
4. Run `./netchecker_gui.exe` from the same MinGW64 shell, or double-click
   it in Explorer (GTK3's DLLs must be on PATH — running it from the
   MinGW64 shell guarantees that; for a standalone .exe you'll want to
   bundle the GTK3 runtime DLLs alongside it, e.g. with `ldd` + copy, or a
   tool like `gtk3-nsis-pack`).

The `ping`-based device check (`ping -n 3 -w 1000 <ip> >nul`) is unchanged
from the original and is Windows-syntax, so it works as-is once built with
MinGW on Windows.

## Build on Linux (for testing the GTK logic itself)

```
sudo apt install libgtk-3-dev
make
./netchecker_gui
```

Note: the ping command uses Windows `ping` flags (`-n`, `-w`), so the
monitor's ping check itself will not work correctly on Linux without
adjusting the command string to `ping -c 3 -W 1 <ip> >/dev/null` — that one
line lives in `FunctionToCheckDevices()`. Everything else in the GUI (add/
edit/delete/search devices and users, CSV persistence, login) works
cross-platform as shipped.

## What changed from the console version

- Console `printf`/`scanf` menus → GTK dialogs and two tabs (Devices, Users)
  with `GtkTreeView` lists and toolbar buttons.
- `MessageBox()` alerts → `GtkMessageDialog`; `Beep()` is kept on Windows,
  `gdk_beep()` is used elsewhere.
- The monitor thread still runs on a `pthread`, same ping-based logic and
  same mutex-protected `Device_List`; it now pushes UI updates back to the
  GTK main thread via `g_idle_add()` (GTK widgets aren't safe to touch from
  a background thread).
- Three small correctness fixes were made while porting (flagged with
  `PORT FIX` comments in the source): `readUsers()` now actually loads
  previously-registered users into the GUI on relaunch, `saveUsers()` only
  writes the valid entries instead of all `MAX_USERS` slots, and `SignIn()`
  returns a result instead of recursing into the login dialog.

Everything else — the struct layouts, the CSV formats, the add/edit/delete/
search logic, the alert-on-status-change logic, the mutex locking pattern —
is the same as the original.
