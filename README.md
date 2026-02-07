# PurpleReaction

Minimal Windows reaction-time measurement tool in C++ with a low-latency architecture.

## What It Does

- Uses `Win32` + `DirectX 11` (no UI frameworks).
- Uses `Raw Input` (`WM_INPUT`) for keyboard/mouse press capture.
- Uses `QueryPerformanceCounter` for timing.
- Runs test trials in exclusive fullscreen with `Present(1, 0)` (VSync).
- Displays only black and white frames (no animation/layout/UI controls).
- Computes reaction time as:
  - `input_timestamp - stimulus_timestamp`
- Supports multi-trial runs and prints per-trial + average results to console.
- Includes a console UX:
  - Main menu
  - Settings page
  - About page
  - Redo-test flow

## Requirements

Hard requirements:

- Windows 10/11 (64-bit)
- GPU + driver with DirectX 11 support
- Visual Studio with C++ desktop tooling installed
- CMake 3.20+
- DirectX 11 runtime (already present on modern Windows)

Required Visual Studio components:

- `Desktop development with C++` workload
- MSVC x64/x86 C++ build tools (v143/v180 or matching your VS install)
- Windows 10/11 SDK

## Recommendations

For best results and fewer build/runtime issues:

- Build from a local Windows path (for example `C:\dev\PurpleReaction`) instead of UNC paths like `\\wsl.localhost\...`.
- Use a release build (`--config Release`).
- Close heavy background apps during testing (browsers, game launchers, updates).
- Keep display refresh stable (avoid changing refresh rate mid-session).
- Use fullscreen on the primary monitor only while testing.
- Use the same input device (same mouse/keyboard) across comparison runs.

## Build (Visual Studio Generator)

Open a Developer PowerShell/Prompt and run from repo root:

```powershell
cmake -S . -B build-vs18 -G "Visual Studio 18 2026" -A x64
cmake --build build-vs18 --config Release
```

Run:

```powershell
.\build-vs18\Release\PurpleReaction.exe
```

If your machine uses an older VS generator, replace the generator name accordingly (for example `Visual Studio 17 2022`).

## Runtime Options (CLI)

```text
PurpleReaction.exe [--min-delay seconds] [--max-delay seconds] [--trials count]
```

Defaults:

- `--min-delay 2.0`
- `--max-delay 5.0`
- `--trials 10`

Example:

```powershell
.\build-vs18\Release\PurpleReaction.exe --min-delay 1.5 --max-delay 4.0 --trials 20
```

## In-App UX

Main menu:

1. Start test
2. Settings
3. About
4. Quit

During a test:

- Black screen: waiting random delay
- White screen: stimulus is visible; press any key or mouse button
- `Esc`: abort current run and return to menu

After a run:

1. Redo test
2. Back to main menu
3. Quit

## Accuracy Notes

- Timing source is `QueryPerformanceCounter` only.
- Stimulus timestamp is sampled around the VSync-blocking `Present` call (midpoint of pre/post QPC captures).
- Input is captured through Raw Input events, not `WM_KEYDOWN`.
- Process/thread priority are raised during active test runs.
- Rendering is intentionally minimal to reduce scheduling/render variability.

## Troubleshooting

### Generator mismatch error

If CMake says generator does not match previous one, use a different build folder or delete old cache:

```powershell
Remove-Item -Recurse -Force .\build-vs18
```

### VS toolset mismatch (example: v143 not found)

Use the generator matching your installed Visual Studio version, or install the missing toolset via Visual Studio Installer.

### UNC path warnings (`\\wsl.localhost\...`)

Building from UNC paths can produce MSBuild incremental warnings (`MSB8064/MSB8065`).  
Build usually still succeeds, but for cleaner behavior copy to a local path like `C:\dev\PurpleReaction`.

## Project Layout

- `CMakeLists.txt` - build config
- `src/main.cpp` - full application implementation
