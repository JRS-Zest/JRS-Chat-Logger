# JRSChatCPP (source package for GitHub)

This folder is a minimized source package extracted for GitHub upload.

## Included

- `CMakeLists.txt`
- `src/`
- `resources/`
- `sounds/` (required notification sounds)
- `scripts/`
- `guild_tab_help.txt`
- `SOUND_FILES.md`

## Build requirements

- Windows
- Visual Studio 2022 Build Tools (MSVC v143)
- CMake 3.20+
- `boost-regex` package discoverable by CMake (`find_package(boost_regex CONFIG REQUIRED)`)

## Runtime requirements

- Npcap (required for packet capture via `wpcap.dll`)
- `sounds/` folder placed next to the executable (included in this repository)

If Npcap is not installed, packet capture will not start.

## Build

```powershell
pwsh ./scripts/build.ps1 -Configuration Release
```

Executable output:

- `build/Release/JRSChatCPP.exe`

## Notes

- Runtime-generated data (`DC/`, `logger_config.ini`) is intentionally excluded by `.gitignore`.
- Sound file names and mapping are documented in `SOUND_FILES.md`.
- This repository is the main app. When you use PriceViewer together with it, place the PriceViewer folder next to this app folder and keep the executable inside it.

Example:

```text
JRSChatCPP/
	JRSChatCPP.exe
	sounds/
	露店価格Viewer/
		露店価格Viewer.exe
		categories.json
```

- `sounds/` contains the notification WAV files used by this app.
- `露店価格Viewer/` is the folder expected for the companion price viewer.
- This package is intended for source distribution and reproducible builds.
