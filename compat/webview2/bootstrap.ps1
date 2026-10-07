param(
    [string]$BuildDir = "",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $Root ".build"
}
if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}

$cmake = Get-Command cmake -ErrorAction Stop
& $cmake.Source -S $Root -B $BuildDir -D CMAKE_BUILD_TYPE=Release
& $cmake.Source --build $BuildDir --config Release

$Dll = Join-Path $Root "bin\vdesktop_webview2.dll"
if (-not (Test-Path $Dll)) {
    throw "Build completed but $Dll was not produced."
}
Write-Host "WebView2 backend ready: $Dll"
Write-Host "Copy the DLL beside the application executable or set VDESKTOP_WEBVIEW2_DLL=$Dll"
