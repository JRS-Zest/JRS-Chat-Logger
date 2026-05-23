param(
    [string]$Configuration = "Release",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Arch = "x64"
)

Write-Host "Building project (Configuration=$Configuration, Generator=$Generator, Arch=$Arch)"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $root\..\

if (-not (Test-Path build)) { New-Item -ItemType Directory -Path build | Out-Null }

Write-Host "Configuring with CMake..."
cmake -G "$Generator" -A $Arch -S . -B build -DCMAKE_BUILD_TYPE=$Configuration
if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed"; Pop-Location; exit $LASTEXITCODE }

Write-Host "Building..."
cmake --build build --config $Configuration -- /m
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; Pop-Location; exit $LASTEXITCODE }

Write-Host "Build finished. Output under build\$Configuration (or build/Release)"
Pop-Location
