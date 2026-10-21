# Validates the POMAN VST3 with Tracktion's pluginval (https://github.com/Tracktion/pluginval, GPLv3,
# used as an external tool only; it is not shipped with POMAN). Downloads the pinned release on first
# use into build/pluginval.
# Usage: powershell -ExecutionPolicy Bypass -File Tools/pluginval.ps1 [-Strictness 5] [-Build build]
param([int]$Strictness = 5, [string]$Build = "build", [string]$Version = "v1.0.4")

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$toolDir = Join-Path $Build "pluginval"
$exe = Join-Path $toolDir "pluginval.exe"
if (-not (Test-Path $exe)) {
    New-Item -ItemType Directory -Force $toolDir | Out-Null
    $zip = Join-Path $toolDir "pluginval_Windows.zip"
    $url = "https://github.com/Tracktion/pluginval/releases/download/$Version/pluginval_Windows.zip"
    Write-Host "Downloading pluginval $Version from $url"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $toolDir -Force
}

$plugin = Join-Path $Build "POMAN_artefacts/Release/VST3/POMAN.vst3"
if (-not (Test-Path $plugin)) { throw "Build the VST3 first: $plugin" }

$logDir = Join-Path $Build "pluginval-logs"
New-Item -ItemType Directory -Force $logDir | Out-Null
& $exe --strictness-level $Strictness --validate-in-process --skip-gui-tests --output-dir $logDir --validate $plugin
if ($LASTEXITCODE -ne 0) {
    Write-Host "pluginval FAILED (exit $LASTEXITCODE); logs in $logDir"
    exit $LASTEXITCODE
}
Write-Host "pluginval passed at strictness $Strictness; logs in $logDir"
