# JRSChatCPP (source package for GitHub)

This folder is a minimized source package extracted for GitHub upload.

## Included

- `CMakeLists.txt`
- `src/`
- `resources/`
- `scripts/`
- `guild_tab_help.txt`

## Build requirements

- Windows
- Visual Studio 2022 Build Tools (MSVC v143)
- CMake 3.20+
- `boost-regex` package discoverable by CMake (`find_package(boost_regex CONFIG REQUIRED)`)

## Build

```powershell
pwsh ./scripts/build.ps1 -Configuration Release
```

Executable output:

- `build/Release/JRSChatCPP.exe`

## Notes

- Runtime-generated data (`sounds/`, `DC/`, `logger_config.ini`) is intentionally excluded by `.gitignore`.
- This package is intended for source distribution and reproducible builds.
