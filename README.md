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
- Supports CSV export of trial results after each completed run.
- Supports non-interactive single-run mode (`--run-once`) with JSON/CSV output for external control UIs.
- Includes a console UX:
  - Main menu
  - Settings page
  - About page
  - Redo-test flow
- Includes an experimental WinUI 3 control-shell project at `control-ui/PurpleReaction.ControlUI`.

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

## Build (Visual Studio Solution)

Open `PurpleReaction.sln` in Visual Studio 2026 and select `x64` + (`Debug` or `Release`), then build the solution.

Projects in the solution:

- `PurpleReaction.Native` (native runner; outputs `build-vs18\<Configuration>\PurpleReaction.exe`)
- `PurpleReaction.ControlUI` (WinUI control shell)

Recommended startup project:

- `PurpleReaction.ControlUI` for control shell workflow
- `PurpleReaction.Native` for direct runner debugging

## Shipping / Packaging

Use the packaging script from a Visual Studio Developer PowerShell:

```powershell
.\scripts\package-release.ps1 -Configuration Release
```

What it does:

- Publishes `PurpleReaction.ControlUI` as a self-contained `win-x64` app.
- Builds `PurpleReaction.Native` in the selected configuration.
- Assembles a release folder containing:
  - `PurpleReaction.ControlUI.exe`
  - `PurpleReaction.exe` (native fullscreen timing runner)
  - `README.md`
  - `LICENSE`
  - `appicon.jpg`
- Generates a zip archive under `artifacts\`.

Output paths:

- Folder: `artifacts\release\<Configuration>\`
- Zip: `artifacts\PurpleReaction-<Configuration>-win-x64.zip`

## MSIX (Optional)

You can also generate an unsigned sideload MSIX from a Visual Studio Developer PowerShell:

```powershell
.\scripts\package-msix.ps1 -Configuration Release
```

Output path:

- `artifacts\msix\<Configuration>\`

Notes:

- This script builds an unsigned MSIX for local/testing sideload flows.
- For production distribution, sign the MSIX with a trusted certificate.

## Runtime Options (CLI)

```text
PurpleReaction.exe [--min-delay seconds] [--max-delay seconds] [--trials count]
                   [--run-once] [--json-out path] [--csv-out path]
```

Defaults:

- `--min-delay 2.0`
- `--max-delay 5.0`
- `--trials 10`

Example:

```powershell
.\build-vs18\Release\PurpleReaction.exe --min-delay 1.5 --max-delay 4.0 --trials 20
```

Non-interactive single-run example (for control UI orchestration):

```powershell
.\build-vs18\Release\PurpleReaction.exe --run-once --min-delay 2.0 --max-delay 5.0 --trials 10 --json-out .\latest.json
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
- Input before white is recorded as a false start for that trial
- `Esc`: abort current run and return to menu

After a run:

1. Redo test
2. Back to main menu
3. Quit

CSV export prompt appears after every completed run:

1. Export to default filename
2. Export to custom path
3. Skip

## Accuracy Notes

- Timing source is `QueryPerformanceCounter` only.
- Stimulus timestamp is sampled around the VSync-blocking `Present` call (midpoint of pre/post QPC captures).
- Input is captured through Raw Input events, not `WM_KEYDOWN`.
- Process/thread priority are raised during active test runs.
- Rendering is intentionally minimal to reduce scheduling/render variability.
- The `--run-once` mode uses the same timing/render/input path as interactive mode; it only bypasses console prompts/menu flow.

## WinUI Control UI (Experimental)

Location:

- `control-ui/PurpleReaction.ControlUI`

Purpose:

- Keep control/config/result browsing in a normal desktop UI.
- Keep measurement execution in the existing fullscreen low-latency runner.

Build and run from repo root:

```powershell
dotnet build .\control-ui\PurpleReaction.ControlUI\PurpleReaction.ControlUI.csproj -c Release
dotnet run --project .\control-ui\PurpleReaction.ControlUI\PurpleReaction.ControlUI.csproj -c Release
```

The WinUI app launches `PurpleReaction.exe --run-once ... --json-out <temp file>`, waits for completion, then reads/displays results.
In solution builds, it auto-discovers the native runner at `build-vs18\Release\PurpleReaction.exe` or `build-vs18\Debug\PurpleReaction.exe`.
In packaged builds, it first tries `PurpleReaction.exe` next to `PurpleReaction.ControlUI.exe`.

## CSV Output

Generated CSV schema:

```text
trial,random_delay_seconds,reaction_ms,false_start
1,2.734901,184.520000,0
2,3.118020,,1
...
average,,192.928500,
```

Default filename format:

- `PurpleReaction_YYYYMMDD_HHMMSS.csv`

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
- `PurpleReaction.sln` - Visual Studio solution (native runner + control UI)
- `src/main.cpp` - full application implementation
- `control-ui/PurpleReaction.ControlUI` - WinUI 3 control-shell (experimental)
- `vs/PurpleReaction.Native` - Visual Studio native C++ project for the runner
- `scripts/package-release.ps1` - release packaging script
