<#
.SYNOPSIS
    Installs, creates and runs the Android emulators used as test devices (ADR-0038).

.DESCRIPTION
    install  Accepts the SDK licenses, installs the emulator, the AEHD hypervisor driver package and
             the system images, then installs the AEHD driver service (needs an elevated shell).
    create   Creates one AVD per API level with the same phone profile. AVDs live under
             <SDK>\avd so that their multi-gigabyte disks stay next to the SDK.
    start    Boots an AVD in the background and waits until Android reports boot completion.
    stop     Shuts a running emulator down.
    status   Shows the acceleration check, the AVDs and the attached devices.

    Each API level has a fixed console port, hence a fixed adb serial:
    API 28 -> emulator-5554, API 35 -> emulator-5556.

.EXAMPLE
    . .\tools\dev-env.ps1
    .\tools\emulator.ps1 install
    .\tools\emulator.ps1 create
    .\tools\emulator.ps1 start -Api 35
    .\tools\emulator.ps1 stop -Api 35
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)]
    [ValidateSet('install', 'create', 'start', 'stop', 'status')]
    [string]$Command,

    [ValidateSet(28, 35)]
    [int]$Api = 35,

    [switch]$Headless,

    [int]$BootTimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'

$ApiLevels = @(28, 35)
$Ports = @{ 28 = 5554; 35 = 5556 }

$Sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } elseif ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else {
    throw 'ANDROID_HOME is not set: point it at the Android SDK (Android Studio puts it in %LOCALAPPDATA%\Android\Sdk).'
}
$env:ANDROID_AVD_HOME = Join-Path $Sdk 'avd'

$SdkManager = Join-Path $Sdk 'cmdline-tools\latest\bin\sdkmanager.bat'
$AvdManager = Join-Path $Sdk 'cmdline-tools\latest\bin\avdmanager.bat'
$Emulator = Join-Path $Sdk 'emulator\emulator.exe'
$Adb = Join-Path $Sdk 'platform-tools\adb.exe'

function Get-ImagePackage([int]$Level) { "system-images;android-$Level;google_apis;x86_64" }
function Get-AvdName([int]$Level) { "gs_api$Level" }
function Get-Serial([int]$Level) { "emulator-$($Ports[$Level])" }

function Assert-Java {
    if (-not $env:JAVA_HOME) { throw 'JAVA_HOME is not set: run . .\tools\dev-env.ps1 first.' }
}

function Assert-Elevated {
    $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Installing the AEHD driver service needs an elevated (administrator) shell.'
    }
}

function Install-Emulator {
    Assert-Java
    $answers = ('y' + [Environment]::NewLine) * 32
    $answers | & $SdkManager --licenses | Out-Null
    $packages = @('emulator', 'extras;google;Android_Emulator_Hypervisor_Driver') +
        ($ApiLevels | ForEach-Object { Get-ImagePackage $_ })
    & $SdkManager --install @packages
    if ($LASTEXITCODE -ne 0) { throw 'sdkmanager failed to install the emulator packages.' }

    $service = Get-Service -Name 'aehd' -ErrorAction SilentlyContinue
    if (-not $service -or $service.Status -ne 'Running') {
        Assert-Elevated
        $installer = Join-Path $Sdk 'extras\google\Android_Emulator_Hypervisor_Driver\silent_install.bat'
        & cmd.exe /d /c "`"$installer`""
        if ($LASTEXITCODE -ne 0) { throw "AEHD driver installation failed: $installer" }
    }
    & $Emulator -accel-check
}

function New-TestAvds {
    Assert-Java
    New-Item -ItemType Directory -Force -Path $env:ANDROID_AVD_HOME | Out-Null
    foreach ($level in $ApiLevels) {
        $name = Get-AvdName $level
        'no' | & $AvdManager create avd --force --name $name --package (Get-ImagePackage $level) --device 'pixel_7' | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "avdmanager failed to create $name." }

        $config = Join-Path $env:ANDROID_AVD_HOME "$name.avd\config.ini"
        $settings = [ordered]@{
            'hw.ramSize'               = '4096'
            'hw.keyboard'              = 'yes'
            'hw.gpu.enabled'           = 'yes'
            'hw.gpu.mode'              = 'auto'
            'disk.dataPartition.size'  = '8G'
            'fastboot.forceColdBoot'   = 'yes'
        }
        $lines = Get-Content $config | Where-Object { $_ -notmatch "^($(($settings.Keys | ForEach-Object { [regex]::Escape($_) }) -join '|'))\s*=" }
        $lines += $settings.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }
        Set-Content -Path $config -Value $lines -Encoding ascii
        Write-Host "Created $name ($(Get-Serial $level))"
    }
}

function Start-TestAvd([int]$Level) {
    $serial = Get-Serial $Level
    $arguments = @('-avd', (Get-AvdName $Level), '-port', $Ports[$Level], '-no-snapshot', '-no-boot-anim')
    if ($Headless) { $arguments += '-no-window' }
    Start-Process -FilePath $Emulator -ArgumentList $arguments -WindowStyle Minimized | Out-Null

    $boot = Start-Job -ScriptBlock {
        param($adb, $serial)
        & $adb -s $serial wait-for-device
        & $adb -s $serial shell 'while [ "$(getprop sys.boot_completed)" != "1" ]; do sleep 1; done'
    } -ArgumentList $Adb, $serial
    if (-not (Wait-Job $boot -Timeout $BootTimeoutSeconds)) {
        Stop-Job $boot
        throw "$serial did not finish booting within $BootTimeoutSeconds s."
    }
    Remove-Job $boot
    $release = & $Adb -s $serial shell getprop ro.build.version.release
    Write-Host "$serial booted (Android $release)"
}

function Stop-TestAvd([int]$Level) {
    & $Adb -s (Get-Serial $Level) emu kill
}

function Show-Status {
    & $Emulator -accel-check
    Write-Host '--- AVDs'
    & $Emulator -list-avds
    Write-Host '--- adb devices'
    & $Adb devices -l
}

switch ($Command) {
    'install' { Install-Emulator }
    'create' { New-TestAvds }
    'start' { Start-TestAvd $Api }
    'stop' { Stop-TestAvd $Api }
    'status' { Show-Status }
}
