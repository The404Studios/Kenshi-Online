# ImGui Setup for Kenshi Online

This guide explains how to download and integrate the full ImGui library into the Kenshi Online mod.

## Quick Setup (Automated)

Just run this command:

```batch
setup_all.bat
```

This will automatically:
1. Download ImGui v1.90.1 from GitHub
2. Extract and copy all necessary files
3. Update the Visual Studio project file
4. Update DirectX11Hook.cpp to use real backends

## Manual Setup

If you prefer to do it step-by-step:

### Step 1: Download ImGui
```batch
setup_imgui.bat
```

This downloads ImGui and copies these files to `KenshiOnlineMod/imgui/`:
- **Core Files**: `imgui.h`, `imgui.cpp`, `imgui_draw.cpp`, `imgui_tables.cpp`, `imgui_widgets.cpp`
- **Backends**: `imgui_impl_dx11.h/cpp`, `imgui_impl_win32.h/cpp`

### Step 2: Update Project
```batch
update_project.bat
```

This updates `KenshiOnlineMod.vcxproj` to include all ImGui source files.

### Step 3: Build
1. Open `Kenshi-Online.sln` in Visual Studio
2. Select **Release | x64**
3. Build Solution (Ctrl+Shift+B)

## What Gets Downloaded

**ImGui Version**: v1.90.1 (stable release)

**Downloaded from**: https://github.com/ocornut/imgui

**Files Installed**:
```
KenshiOnlineMod/imgui/
├── imgui.h                          (Core header)
├── imgui.cpp                        (Core implementation)
├── imgui_draw.cpp                   (Drawing primitives)
├── imgui_tables.cpp                 (Table widgets)
├── imgui_widgets.cpp                (UI widgets)
├── imgui_internal.h                 (Internal API)
├── imconfig.h                       (Configuration)
├── imstb_rectpack.h                 (STB rectangle packer)
├── imstb_textedit.h                 (STB text editor)
├── imstb_truetype.h                 (STB TrueType font)
└── backends/
    ├── imgui_impl_dx11.h            (DirectX 11 rendering)
    ├── imgui_impl_dx11.cpp
    ├── imgui_impl_win32.h           (Windows input handling)
    └── imgui_impl_win32.cpp
```

## How It Works

### Before Setup (Stubs)
The project initially contains minimal stub implementations in:
- `imgui/imgui.h` (basic declarations only)
- `imgui/imgui.cpp` (empty functions that do nothing)

These stubs allow the code to **compile** but the UI **won't render**.

### After Setup (Full Library)
The batch files download the official ImGui library which:
- Renders actual UI widgets (buttons, text fields, etc.)
- Handles DirectX 11 drawing commands
- Processes Windows input (mouse, keyboard)
- Manages UI layout and styling

Your old stub files are backed up as:
- `imgui/imgui.h.stub`
- `imgui/imgui.cpp.stub`

## Troubleshooting

### "Failed to download ImGui"
- Check your internet connection
- Make sure PowerShell is available
- Try downloading manually from: https://github.com/ocornut/imgui/releases

### Build Errors
If you get compiler errors after setup:
1. Make sure you ran `update_project.bat`
2. Close and reopen Visual Studio
3. Right-click solution → Reload Project
4. Clean Solution (Ctrl+Shift+B → Clean)
5. Rebuild Solution

### "Cannot find imgui_impl_dx11.h"
Make sure the `backends` folder exists:
```
KenshiOnlineMod/imgui/backends/
```

If missing, re-run `setup_imgui.bat`.

## Testing the UI

1. **Compile** the mod DLL
2. **Inject** into Kenshi.exe (use a DLL injector)
3. **Wait 5 seconds** (mod initialization)
4. **Login screen should appear** as an overlay
5. **Press F1** to toggle UI visibility

## What the UI Shows

- **On startup**: Login screen (username/password fields)
- **After login**: Main menu with server browser, friends list, settings
- **F1 key**: Toggles between visible/hidden

## Project Structure

```
Kenshi-Online/
├── setup_all.bat                    (Master setup script)
├── setup_imgui.bat                  (Downloads ImGui)
├── update_project.bat               (Updates vcxproj)
├── README_IMGUI_SETUP.md           (This file)
└── KenshiOnlineMod/
    ├── KenshiOnlineMod.vcxproj     (Visual Studio project)
    ├── DirectX11Hook.cpp           (Hooks into Kenshi's renderer)
    ├── ImGuiUI.cpp                 (UI implementation)
    └── imgui/                      (ImGui library)
        ├── imgui.h
        ├── imgui.cpp
        ├── imgui_draw.cpp
        ├── imgui_tables.cpp
        ├── imgui_widgets.cpp
        └── backends/
            ├── imgui_impl_dx11.cpp
            └── imgui_impl_win32.cpp
```

## Dependencies

The setup scripts require:
- **Windows 7+** (for PowerShell)
- **Internet connection** (to download ImGui)
- **Visual Studio 2019+** (to compile)

## License

ImGui is licensed under MIT License.
See: https://github.com/ocornut/imgui/blob/master/LICENSE.txt
