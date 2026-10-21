# Code-signs POMAN.exe and the VST3 with your Authenticode certificate (needs the Windows SDK's
# signtool). Signed builds stop SmartScreen and antivirus warnings from building up a reputation
# from scratch with every release. POMAN does not ship a certificate: buy one (OV or EV) from a
# certificate authority, then either install it in your certificate store and pass its thumbprint,
# or pass the .pfx file (you are asked for its password; it is never stored).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File Tools/sign.ps1 -Thumbprint <SHA1 of your certificate>
#   powershell -ExecutionPolicy Bypass -File Tools/sign.ps1 -Pfx C:\path\to\certificate.pfx
param(
    [string]$Thumbprint,
    [string]$Pfx,
    [string]$Build = "build",
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not $Thumbprint -and -not $Pfx) { throw "Pass -Thumbprint or -Pfx (see the header of this script)." }

$signtool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $signtool) { throw "signtool.exe not found: install the Windows 10/11 SDK." }

$files = @(
    "$Build/POMAN_artefacts/Release/Standalone/POMAN.exe",
    "$Build/POMAN_artefacts/Release/VST3/POMAN.vst3/Contents/x86_64-win/POMAN.vst3"
) | Where-Object { Test-Path $_ }
if ($files.Count -eq 0) { throw "Nothing to sign: build POMAN first." }

$common = @("sign", "/fd", "sha256", "/tr", $TimestampUrl, "/td", "sha256", "/d", "POMAN")
if ($Thumbprint) {
    $signArgs = $common + @("/sha1", $Thumbprint) + $files
} else {
    $password = Read-Host "Password for $Pfx" -AsSecureString
    $plain = [Runtime.InteropServices.Marshal]::PtrToStringAuto([Runtime.InteropServices.Marshal]::SecureStringToBSTR($password))
    $signArgs = $common + @("/f", $Pfx, "/p", $plain) + $files
}
& $signtool.FullName @signArgs
if ($LASTEXITCODE -ne 0) { throw "signtool failed ($LASTEXITCODE)" }
& $signtool.FullName verify /pa /v @files | Select-String "Successfully verified|Number of errors"
Write-Host "Signed: $($files -join ', '). Copy them into dist/ and portable/ before packaging."
