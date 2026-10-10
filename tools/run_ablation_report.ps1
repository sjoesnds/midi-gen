param(
    [ValidateRange(1, 4096)]
    [int]$Seeds = 256,
    [string]$Output = "ablation-report.csv"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    cmake -S tests -B build-qa -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }

    cmake --build build-qa --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "QA build failed with exit code $LASTEXITCODE" }

    $qa = Get-ChildItem -Path "build-qa" -Filter "MidiForgeQA.exe" -Recurse -File |
        Select-Object -First 1
    if (-not $qa) { throw "Could not find MidiForgeQA.exe after building the QA target" }

    & $qa.FullName --ablation-report $Seeds $Output
    if ($LASTEXITCODE -ne 0) { throw "Ablation report failed with exit code $LASTEXITCODE" }

    Write-Host "Ablation report written to $Output"
}
finally {
    Pop-Location
}
