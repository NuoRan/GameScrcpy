<#
.SYNOPSIS
    Imports the MSVC x64 developer environment and the Qt kit location into the current PowerShell session.

.EXAMPLE
    $env:GS_QT_DIR = 'C:\Qt\6.10.1\msvc2022_64'
    . .\tools\dev-env.ps1
    cmake --workflow --preset msvc-debug
#>
[CmdletBinding()]
param(
    [string]$QtDir = $env:GS_QT_DIR
)

$ErrorActionPreference = 'Stop'

function Find-VcVars {
    $roots = @("$env:ProgramFiles\Microsoft Visual Studio", "${env:ProgramFiles(x86)}\Microsoft Visual Studio")
    foreach ($root in $roots) {
        if (-not (Test-Path $root)) { continue }
        $candidate = Get-ChildItem -Path "$root\*\*\VC\Auxiliary\Build\vcvars64.bat" -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($candidate) { return $candidate.FullName }
    }
    throw 'vcvars64.bat not found: install Visual Studio 2022 or newer with the "Desktop development with C++" workload.'
}

if ($env:GS_DEV_ENV_LOADED -ne '1') {
    $vcvars = Find-VcVars
    $lines = & cmd.exe /d /s /c "`"$vcvars`" >nul && set"
    if ($LASTEXITCODE -ne 0) { throw "vcvars64.bat failed: $vcvars" }
    foreach ($line in $lines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            Set-Item -Path "Env:$($line.Substring(0, $separator))" -Value $line.Substring($separator + 1)
        }
    }
    $env:GS_DEV_ENV_LOADED = '1'
}

if ($QtDir) {
    if (-not (Test-Path (Join-Path $QtDir 'lib\cmake\Qt6'))) {
        throw "GS_QT_DIR does not point to a Qt 6 kit: $QtDir"
    }
    $env:GS_QT_DIR = $QtDir
    $qtBin = Join-Path $QtDir 'bin'
    if (-not (($env:PATH -split ';') -contains $qtBin)) { $env:PATH = "$qtBin;$env:PATH" }
} else {
    Write-Warning 'GS_QT_DIR is not set: only the core-only preset can be configured.'
}

if (-not $env:JAVA_HOME) {
    $jdk = Get-ChildItem -Path "$env:ProgramFiles\Java", "$env:ProgramFiles\Eclipse Adoptium", "$env:ProgramFiles\Microsoft" `
            -Directory -Filter 'jdk-17*' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($jdk) { $env:JAVA_HOME = $jdk.FullName }
}
if (-not $env:ANDROID_HOME) {
    Write-Warning 'ANDROID_HOME is not set: the android/ Gradle build will not find the SDK.'
}

$ninja = (Get-Command ninja -ErrorAction SilentlyContinue).Source
Write-Host "MSVC toolset : $env:VCToolsVersion"
Write-Host "Qt kit       : $env:GS_QT_DIR"
Write-Host "Ninja        : $ninja"
Write-Host "JDK          : $env:JAVA_HOME"
Write-Host "Android SDK  : $env:ANDROID_HOME"
