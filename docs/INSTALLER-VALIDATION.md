# Windows installer validation

The release `install.bat` and `uninstall.bat` accept an optional Kenshi path as
their first argument. Pass `/quiet` as the second argument to suppress `pause`
and interactive path prompts during validation:

```powershell
cmd /d /c "install.bat ""C:\Games\Kenshi Test"" /quiet"
cmd /d /c "uninstall.bat ""C:\Games\Kenshi Test"" /quiet"
```

Exit codes are stable and intended for diagnostics:

| Code | Meaning |
| ---: | --- |
| `0` | The requested operation completed, including an already-uninstalled state. |
| `1` | The Kenshi path is missing or invalid. |
| `2` | The extracted release package is incomplete or invalid. |
| `3` | Kenshi is running or required locations are not writable. |
| `4` | An install, verification, or restore operation failed. |

## Isolated validation procedure

Never test against a real Kenshi installation when validating destructive error
paths. Instead, extract `KenshiMP-Install.zip` into a temporary package directory
and create a fake Kenshi tree containing:

```text
kenshi_x64.exe
Plugins_x64.cfg
data\__mods.list
data\gui\layout\Kenshi_MainMenu.layout
mods\
```

Record hashes of the original config, mod list, and main-menu layout. Run these
cases and check both the exit code and resulting filesystem state:

1. Install from a package directory whose path contains spaces; expect `0`.
2. Install again; expect `0`, one plugin line, one mod-list line, and unchanged
   original backups.
3. Uninstall; expect `0`, restored original hashes, and removal of only files
   installed by KenshiMP.
4. Uninstall again; expect `0` and no filesystem changes.
5. Pass a directory without `kenshi_x64.exe`; expect `1` and no changes.
6. Remove or empty one required package input; expect `2` before any Kenshi file
   is changed.
7. Deny write access to the fake Kenshi tree; expect `3` and no installation.
8. Add an unrelated file under `mods\kenshi-online`, then uninstall. The unrelated
   file and directory must remain.
9. Force a destination copy or restore failure. Expect `4`, an explicit error,
   and preserved `.KenshiMP-install-state` diagnostics. A failed first install
   must restore the original files.

The state directory belongs to the hardened installer. The uninstaller performs
no deletion when its `installed.marker` is absent, which prevents it from
guessing ownership of files from older or unrelated installations.
