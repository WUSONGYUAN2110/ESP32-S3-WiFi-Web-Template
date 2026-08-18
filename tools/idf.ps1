$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

# Preserve passthrough arguments before dot-sourcing the ESP-IDF profile.
$idfArguments = @($args)
if ($idfArguments.Count -eq 0) {
    $idfArguments = @("build")
}

$profileName = "Microsoft.v6.0.2.PowerShell_profile.ps1"
$profileCandidates = [System.Collections.Generic.List[string]]::new()

if ($env:ESP_IDF_POWERSHELL_PROFILE) {
    [void]$profileCandidates.Add($env:ESP_IDF_POWERSHELL_PROFILE)
}
if ($env:IDF_TOOLS_PATH) {
    [void]$profileCandidates.Add((Join-Path $env:IDF_TOOLS_PATH $profileName))
}
if ($env:IDF_PATH) {
    $idfParent = Split-Path -Parent $env:IDF_PATH
    [void]$profileCandidates.Add((Join-Path (Join-Path $idfParent "tools") $profileName))
}
# Keep compatibility with the default Windows ESP-IDF installer location.
[void]$profileCandidates.Add((Join-Path ${env:USERPROFILE} ".espressif\$profileName"))
[void]$profileCandidates.Add("C:\Espressif\tools\$profileName")

$activationScript = $profileCandidates |
    Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
    Select-Object -First 1
if (-not $activationScript) {
    throw @"
ESP-IDF v6.0.2 PowerShell profile was not found.
Set ESP_IDF_POWERSHELL_PROFILE to the full profile path, or set IDF_TOOLS_PATH to the ESP-IDF tools directory.
"@
}

. $activationScript

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw "ESP-IDF environment activation completed, but idf.py is unavailable."
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$exitCode = 0

Push-Location -LiteralPath $projectRoot
try {
    idf.py @idfArguments
    $exitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}

if ($exitCode -ne 0) {
    throw "idf.py exited with code $exitCode."
}
