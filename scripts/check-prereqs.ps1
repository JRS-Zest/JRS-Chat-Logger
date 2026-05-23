Write-Host "Checking prerequisites..."

$errors = @()

function Check-Command($cmd, $name) {
    $p = Get-Command $cmd -ErrorAction SilentlyContinue
    if (-not $p) { $script:errors += "$name not found: $cmd" }
    else { Write-Host "$name found: $($p.Path)" }
}

Check-Command -cmd cmake -name "CMake"
Check-Command -cmd git -name "Git"
Check-Command -cmd cl -name "MSVC cl.exe"

if ($errors.Count -eq 0) {
    Write-Host "All basic tools present." -ForegroundColor Green
    exit 0
} else {
    Write-Host "Issues found:" -ForegroundColor Yellow
    $errors | ForEach-Object { Write-Host " - $_" }
    Write-Host "Please install the missing tools (Visual Studio C++ workload, CMake, Git)." -ForegroundColor Red
    exit 1
}
