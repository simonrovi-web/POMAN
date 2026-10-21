# Runs every POMAN test suite against an existing Release build and exits non-zero if any fails.
# Usage: powershell -ExecutionPolicy Bypass -File Tools/run_tests.ps1 [-Build build]
param([string]$Build = "build")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$suites = @(
    @{ Name = "Tuner";       Exe = "$Build/POMANTests_artefacts/Release/POMANTests.exe";               Args = @() },
    @{ Name = "DSP";         Exe = "$Build/POMANDspTests_artefacts/Release/POMANDspTests.exe";         Args = @() },
    @{ Name = "Amp feel";    Exe = "$Build/POMANAmpFeel_artefacts/Release/POMANAmpFeel.exe";           Args = @("--check") },
    @{ Name = "MIDI";        Exe = "$Build/POMANMidiTests_artefacts/Release/POMANMidiTests.exe";       Args = @() },
    @{ Name = "Recorder";    Exe = "$Build/POMANRecorderTests_artefacts/Release/POMANRecorderTests.exe"; Args = @() },
    @{ Name = "NAM";         Exe = "$Build/POMANNamTests_artefacts/Release/POMANNamTests.exe";         Args = @("external/NeuralAmpModelerCore/example_models") }
)

$failed = @()
foreach ($s in $suites) {
    if (-not (Test-Path $s.Exe)) {
        Write-Host "MISSING  $($s.Name): $($s.Exe) (build it first)"
        $failed += $s.Name
        continue
    }
    $output = & $s.Exe @($s.Args) 2>&1
    $code = $LASTEXITCODE
    $output | Where-Object { $_ -match "FAIL" } | ForEach-Object { Write-Host "  $_" }
    if ($code -ne 0) { $failed += $s.Name; Write-Host "FAIL     $($s.Name) (exit $code)" }
    else { Write-Host "PASS     $($s.Name)" }
}

& python Tests/check_presets.py
if ($LASTEXITCODE -ne 0) { $failed += "Presets" }

if ($failed.Count -gt 0) {
    Write-Host "`nFailed: $($failed -join ', ')"
    exit 1
}
Write-Host "`nAll suites passed."
