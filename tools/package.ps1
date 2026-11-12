<#
.SYNOPSIS
    Packs a release build into the portable zip, the installer and the symbols zip of ADR-0089.

.DESCRIPTION
    Gathers what a release needs from build\msvc-release\bin into dist\GameScrcpy-<version>\: GameScrcpy.exe,
    gs.exe, gs-exthost.exe and gs-scripthost.exe, the Qt runtime that windeployqt placed there at build time, the phone apps, the
    built-in components, the licenses and the bundled TypeScript. Adds the Visual C++ runtime from
    Visual Studio's redistributable folder and adb from the Android platform-tools pinned in packaging\adb.json
    (downloaded once into deps\adb, checked against its SHA-256 and the Google signature). Test programs, PDBs and
    the QML debugging plugins stay out. No extension is built in: the packaged gs.exe and cmake\PackSample.cmake pack
    the samples (the PC clock build in examples\pc-clock\package, the CRT scanlines, the slate theme, the deck
    bridge, the voice commands, the event log, the tour and help, the key list, the notes panel and the read aloud
    from examples\extensions) under developer ids
    such as example.pc-clock into sdk\samples\<id>-<version>.gsext, for developers to add with "Install from
    file...". cmake.exe is the one named in the build's CMakeCache.txt, else the one on PATH.

    Every program and DLL in the folder must find what it imports in the folder or in Windows (the Visual C++
    runtime only in the folder), and gs.exe must answer --version with nothing but Windows on PATH. -Smoke also
    starts GameScrcpy.exe --smoke-test from the folder, which shows a window until the first frame.

    Writes dist\GameScrcpy-<version>-portable.zip (with portable.flag), dist\GameScrcpy-<version>-setup.exe
    (Inno Setup compiling packaging\GameScrcpy.iss; skipped with -NoSetup) and dist\GameScrcpy-<version>-symbols.zip.
    ISCC.exe is looked up in GS_ISCC, PATH and the usual Inno Setup 6 and 7 install folders. Nothing is
    built here: build the msvc-release preset first.

.EXAMPLE
    .\tools\package.ps1

.EXAMPLE
    .\tools\package.ps1 -NoSetup -Smoke
#>
[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$OutDir,
    [switch]$NoSetup,
    [switch]$Smoke
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem

$root = Split-Path $PSScriptRoot -Parent
if (-not $BuildDir) { $BuildDir = Join-Path $root 'build\msvc-release' }
if (-not $OutDir) { $OutDir = Join-Path $root 'dist' }
$BuildDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)
$OutDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir)
$bin = Join-Path $BuildDir 'bin'

$Programs = @('GameScrcpy.exe', 'gs.exe', 'gs-exthost.exe', 'gs-scripthost.exe')
$Needed = @('GameScrcpy.exe', 'gs.exe', 'gs-server.apk')
$RootTypes = @('.dll', '.apk', '.json')
$SkippedFolders = @('qmltooling')
$Samples = @(
    @{ Id = 'example.pc-clock'; Folder = Join-Path $BuildDir 'examples\pc-clock\package' },
    @{ Id = 'example.crt-scanlines'; Folder = Join-Path $root 'examples\extensions\crt-scanlines' },
    @{ Id = 'example.slate-theme'; Folder = Join-Path $root 'examples\extensions\slate-theme' },
    @{ Id = 'example.deck-bridge'; Folder = Join-Path $root 'examples\extensions\deck-bridge' },
    @{ Id = 'example.voice-commands'; Folder = Join-Path $root 'examples\extensions\voice-commands' },
    @{ Id = 'example.event-log'; Folder = Join-Path $root 'examples\extensions\event-log' },
    @{ Id = 'example.tour-and-help'; Folder = Join-Path $root 'examples\extensions\tour-and-help' },
    @{ Id = 'example.key-list'; Folder = Join-Path $root 'examples\extensions\key-list' },
    @{ Id = 'example.notes-panel'; Folder = Join-Path $root 'examples\extensions\notes-panel' },
    @{ Id = 'example.read-aloud'; Folder = Join-Path $root 'examples\extensions\read-aloud' }
)
$DroppedTypes = @('.pdb', '.ilk', '.exp', '.lib')
$Symbols = @('GameScrcpy.pdb', 'gs.pdb', 'gs-exthost.pdb', 'gs-scripthost.pdb')
$ShippedOnly = '^(msvcp140.*|vcruntime140.*|concrt140|vccorlib140)\.dll$'

function Assert-ExitCode([string]$What) {
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit code $LASTEXITCODE)" }
}

function Reset-Directory([string]$Path) {
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Recurse -Force }
    New-Item -ItemType Directory -Force $Path | Out-Null
}

function Get-ProjectInfo {
    $text = Get-Content -Raw -LiteralPath (Join-Path $root 'CMakeLists.txt')
    $version = [regex]::Match($text, 'project\(\s*GameScrcpy\s+VERSION\s+(\d+\.\d+\.\d+)')
    if (-not $version.Success) { throw 'CMakeLists.txt gives no project version' }
    $url = [regex]::Match($text, 'HOMEPAGE_URL\s+"([^"]+)"')
    [pscustomobject]@{
        Version = $version.Groups[1].Value
        Url     = $(if ($url.Success) { $url.Groups[1].Value } else { '' })
    }
}

function Assert-ReleaseBuild {
    foreach ($name in $Needed) {
        if (-not (Test-Path -LiteralPath (Join-Path $bin $name))) {
            throw "$name is not in ${bin}: build the msvc-release preset first"
        }
    }
    if (Test-Path -LiteralPath (Join-Path $bin 'Qt6Cored.dll')) { throw "$bin holds a Debug build" }
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $type = Select-String -LiteralPath $cache -Pattern '^CMAKE_BUILD_TYPE:\w+=(.*)$' | Select-Object -First 1
        if ($type -and $type.Matches[0].Groups[1].Value -eq 'Debug') { throw "$BuildDir is a Debug build" }
    }
}

function Find-VcRuntime {
    $candidates = @()
    if ($env:VCToolsRedistDir) { $candidates += Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT' }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($install) {
            $redist = Join-Path $install 'VC\Redist\MSVC'
            if (Test-Path -LiteralPath $redist) {
                $candidates += Get-ChildItem -LiteralPath $redist -Directory |
                    Where-Object { $_.Name -match '^\d+(\.\d+)+$' } |
                    Sort-Object { [version]$_.Name } -Descending |
                    ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' }
            }
        }
    }
    foreach ($folder in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $folder 'vcruntime140.dll')) { return $folder }
    }
    throw 'the Visual C++ runtime (VC\Redist\MSVC\<version>\x64\Microsoft.VC143.CRT) was not found'
}

function Get-Adb {
    $pin = Get-Content -Raw -LiteralPath (Join-Path $root 'packaging\adb.json') | ConvertFrom-Json
    $work = Join-Path $root 'deps\adb'
    $zip = Join-Path $work ('downloads\' + (Split-Path $pin.url -Leaf))
    if (-not (Test-Path -LiteralPath $zip) -or (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $pin.sha256) {
        New-Item -ItemType Directory -Force (Split-Path $zip) | Out-Null
        Write-Host "Downloading $($pin.url)"
        $ProgressPreference = 'SilentlyContinue'
        Invoke-WebRequest -Uri $pin.url -OutFile $zip -UseBasicParsing
        $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
        if ($hash -ne $pin.sha256) {
            Remove-Item -LiteralPath $zip -Force
            throw "$($pin.url) has SHA-256 $hash, packaging\adb.json pins $($pin.sha256)"
        }
    }
    $folder = Join-Path $work "platform-tools-$($pin.version)"
    Reset-Directory $folder
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        foreach ($name in @($pin.files) + 'NOTICE.txt') {
            $entry = $archive.GetEntry("platform-tools/$name")
            if (-not $entry) { throw "$zip holds no platform-tools/$name" }
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $folder $name), $true)
        }
    } finally {
        $archive.Dispose()
    }
    foreach ($name in $pin.files) {
        $signature = Get-AuthenticodeSignature -LiteralPath (Join-Path $folder $name)
        $signer = if ($signature.SignerCertificate) { $signature.SignerCertificate.GetNameInfo('SimpleName', $false) } else { '' }
        if ($signature.Status -ne 'Valid' -or $signer -ne $pin.signer) {
            throw "$name of platform-tools $($pin.version) is not signed by $($pin.signer) ($($signature.Status), $signer)"
        }
    }
    $notice = Join-Path $root 'tools\licenses\Android-SDK-Platform-Tools-NOTICE.txt'
    $shipped = (Get-Content -Raw -LiteralPath (Join-Path $folder 'NOTICE.txt')) -replace "`r`n", "`n"
    $listed = (Get-Content -Raw -LiteralPath $notice) -replace "`r`n", "`n"
    if ($shipped -ne $listed) {
        throw "the NOTICE of platform-tools $($pin.version) differs from ${notice}: copy it there and rebuild"
    }
    [pscustomobject]@{ Folder = $folder; Files = @($pin.files); Version = $pin.version }
}

function Copy-Release([string]$Stage) {
    foreach ($file in Get-ChildItem -LiteralPath $bin -File) {
        $wanted = if ($file.Extension -eq '.exe') { $Programs -contains $file.Name }
                  elseif ($file.Name -like 'gs_*') { $false }
                  else { $RootTypes -contains $file.Extension.ToLowerInvariant() }
        if ($wanted) { Copy-Item -LiteralPath $file.FullName -Destination $Stage }
    }
    foreach ($folder in Get-ChildItem -LiteralPath $bin -Directory) {
        if ($SkippedFolders -contains $folder.Name) { continue }
        Copy-Item -LiteralPath $folder.FullName -Destination (Join-Path $Stage $folder.Name) -Recurse
    }
    Get-ChildItem -LiteralPath $Stage -Recurse -File | Where-Object { $DroppedTypes -contains $_.Extension.ToLowerInvariant() } |
        Remove-Item -Force
}

function Assert-NoCommunity([string]$Stage) {
    $folder = Get-ChildItem -LiteralPath $Stage -Recurse -Directory | Where-Object { $_.Name -eq 'community' } |
        Select-Object -First 1
    if ($folder) { throw "community extensions are never packaged, but $($folder.FullName) is in the package" }
    foreach ($manifest in Get-ChildItem -LiteralPath $Stage -Recurse -File -Filter 'manifest.json') {
        $id = (Get-Content -Raw -Encoding UTF8 -LiteralPath $manifest.FullName | ConvertFrom-Json).id
        if ($id -is [string] -and $id.StartsWith('community.')) {
            throw "the community extension $id is never packaged, but $($manifest.FullName) is in the package"
        }
    }
    Write-Host 'community: no community extension in the package'
}

function Find-CMake {
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $line = Select-String -LiteralPath $cache -Pattern '^CMAKE_COMMAND:\w+=(.*)$' | Select-Object -First 1
        if ($line -and (Test-Path -LiteralPath $line.Matches[0].Groups[1].Value)) { return $line.Matches[0].Groups[1].Value }
    }
    $onPath = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    throw "cmake.exe was not found in $cache or on PATH"
}

function Add-Samples([string]$Stage) {
    foreach ($sample in $Samples) {
        $manifest = Join-Path $sample.Folder 'manifest.json'
        if (-not (Test-Path -LiteralPath $manifest)) { throw "$manifest is missing: build the msvc-release preset first" }
        $sampleVersion = (Get-Content -Raw -Encoding UTF8 -LiteralPath $manifest | ConvertFrom-Json).version
        $package = Join-Path $Stage "sdk\samples\$($sample.Id)-$sampleVersion.gsext"
        $arguments = @("-DGS=$(Join-Path $Stage 'gs.exe')", "-DSOURCE=$($sample.Folder)", "-DID=$($sample.Id)",
                       "-DOUT=$package", '-P', (Join-Path $root 'cmake\PackSample.cmake'))
        & (Find-CMake) $arguments | Out-Host
        Assert-ExitCode "packing the sample $($sample.Id)"
        $package
    }
}

function Assert-Samples([string]$Stage, [string[]]$Packages) {
    $builtIn = Join-Path $Stage 'extensions'
    if (Test-Path -LiteralPath $builtIn) { throw "no extension is built in, but $builtIn is in the package" }
    foreach ($package in $Packages) {
        if (-not (Test-Path -LiteralPath $package -PathType Leaf)) { throw "the sample package $package was not written" }
    }
    Write-Host "samples: $(($Packages | ForEach-Object { Split-Path $_ -Leaf }) -join ', ') in sdk\samples"
}

function Get-PeImports([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { return }
    $pe = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($pe -lt 0 -or $pe + 24 -gt $bytes.Length -or [BitConverter]::ToUInt32($bytes, $pe) -ne 0x4550) { return }
    $sectionCount = [BitConverter]::ToUInt16($bytes, $pe + 6)
    $optional = $pe + 24
    $sections = $optional + [BitConverter]::ToUInt16($bytes, $pe + 20)
    $wide = [BitConverter]::ToUInt16($bytes, $optional) -eq 0x20B
    $directoryCount = [BitConverter]::ToUInt32($bytes, $optional + $(if ($wide) { 108 } else { 92 }))
    $directories = $optional + $(if ($wide) { 112 } else { 96 })
    $offsetOf = {
        param([long]$Rva)
        for ($i = 0; $i -lt $sectionCount; $i++) {
            $section = $sections + 40 * $i
            $start = [long][BitConverter]::ToUInt32($bytes, $section + 12)
            $size = [Math]::Max([long][BitConverter]::ToUInt32($bytes, $section + 8), [long][BitConverter]::ToUInt32($bytes, $section + 16))
            if ($Rva -ge $start -and $Rva -lt $start + $size) {
                return [int]($Rva - $start + [BitConverter]::ToUInt32($bytes, $section + 20))
            }
        }
        -1
    }
    $nameAt = {
        param([int]$Offset)
        $end = [Array]::IndexOf($bytes, [byte]0, $Offset)
        [Text.Encoding]::ASCII.GetString($bytes, $Offset, $end - $Offset)
    }
    if ($directoryCount -gt 1) {
        $rva = [BitConverter]::ToUInt32($bytes, $directories + 8)
        $at = if ($rva -ne 0) { & $offsetOf $rva } else { -1 }
        while ($at -ge 0) {
            $name = [BitConverter]::ToUInt32($bytes, $at + 12)
            if ($name -eq 0) { break }
            [pscustomobject]@{ Name = & $nameAt (& $offsetOf $name); Delayed = $false }
            $at += 20
        }
    }
    if ($directoryCount -gt 13) {
        $rva = [BitConverter]::ToUInt32($bytes, $directories + 13 * 8)
        $at = if ($rva -ne 0) { & $offsetOf $rva } else { -1 }
        while ($at -ge 0) {
            $attributes = [BitConverter]::ToUInt32($bytes, $at)
            $name = [BitConverter]::ToUInt32($bytes, $at + 4)
            if ($name -eq 0) { break }
            if (($attributes -band 1) -ne 0) {
                [pscustomobject]@{ Name = & $nameAt (& $offsetOf $name); Delayed = $true }
            }
            $at += 32
        }
    }
}

function Test-Imports([string]$Stage) {
    $system = [Environment]::GetFolderPath('System')
    $folders = @{}
    $missing = [Collections.Generic.List[string]]::new()
    $checked = 0
    $files = Get-ChildItem -LiteralPath $Stage -Recurse -File | Where-Object { $_.Extension -in '.dll', '.exe' }
    foreach ($file in $files) {
        foreach ($folder in $Stage, $file.DirectoryName) {
            if (-not $folders.ContainsKey($folder)) {
                $names = @{}
                Get-ChildItem -LiteralPath $folder -File -Filter *.dll | ForEach-Object { $names[$_.Name.ToLowerInvariant()] = $true }
                $folders[$folder] = $names
            }
        }
        foreach ($import in Get-PeImports $file.FullName) {
            $name = $import.Name.ToLowerInvariant()
            if ($name.StartsWith('api-ms-win-') -or $name.StartsWith('ext-ms-')) { continue }
            if ($folders[$Stage][$name] -or $folders[$file.DirectoryName][$name]) { continue }
            if ($name -notmatch $ShippedOnly -and (Test-Path -LiteralPath (Join-Path $system $name))) { continue }
            $kind = if ($import.Delayed) { 'delay-loads' } else { 'imports' }
            $missing.Add("$($file.FullName.Substring($Stage.Length + 1)) $kind $($import.Name)")
        }
        $checked++
    }
    if ($missing.Count -gt 0) { throw "missing from the package and from Windows:`n  " + ($missing -join "`n  ") }
    Write-Host "imports: all $checked programs and DLLs find what they import"
}

function Invoke-Isolated([string]$Program, [string[]]$Arguments, [hashtable]$Environment = @{}, [int]$Seconds = 120) {
    $info = [Diagnostics.ProcessStartInfo]::new($Program)
    $info.Arguments = ($Arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.WorkingDirectory = Split-Path $Program
    $info.EnvironmentVariables['PATH'] = [Environment]::GetFolderPath('System') + ';' + [Environment]::GetFolderPath('Windows')
    foreach ($name in 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH',
                      'ANDROID_HOME', 'ANDROID_SDK_ROOT', 'QT_QPA_PLATFORM') {
        $info.EnvironmentVariables.Remove($name)
    }
    foreach ($name in $Environment.Keys) { $info.EnvironmentVariables[$name] = $Environment[$name] }
    $process = [Diagnostics.Process]::Start($info)
    $output = $process.StandardOutput.ReadToEndAsync()
    $errors = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($Seconds * 1000)) {
        $process.Kill()
        throw "$Program $($info.Arguments) did not finish in $Seconds seconds"
    }
    $process.WaitForExit()
    [pscustomobject]@{ Code = $process.ExitCode; Text = $output.Result + $errors.Result }
}

function Find-Iscc {
    $candidates = @()
    if ($env:GS_ISCC) { $candidates += $env:GS_ISCC }
    $onPath = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($onPath) { $candidates += $onPath.Source }
    foreach ($base in $env:ProgramFiles, ${env:ProgramFiles(x86)}, (Join-Path $env:LOCALAPPDATA 'Programs')) {
        if ($base) { $candidates += (Join-Path $base 'Inno Setup 7\ISCC.exe'), (Join-Path $base 'Inno Setup 6\ISCC.exe') }
    }
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }
    throw 'ISCC.exe (Inno Setup) was not found: install Inno Setup 6 or 7, set GS_ISCC to its ISCC.exe, or pass -NoSetup'
}

function Get-FolderEntries([string]$Folder, [string]$Prefix) {
    Get-ChildItem -LiteralPath $Folder -Recurse -File | ForEach-Object {
        [pscustomobject]@{ File = $_.FullName; Name = $Prefix + $_.FullName.Substring($Folder.Length + 1).Replace('\', '/') }
    }
}

function Write-Zip([string]$Path, [object[]]$Entries) {
    if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Force }
    $archive = [IO.Compression.ZipFile]::Open($Path, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($entry in $Entries) {
            if ($entry.File) {
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $entry.File, $entry.Name,
                    [IO.Compression.CompressionLevel]::Optimal)
            } else {
                [void]$archive.CreateEntry($entry.Name)
            }
        }
    } finally {
        $archive.Dispose()
    }
}

$project = Get-ProjectInfo
$version = $project.Version
Assert-ReleaseBuild
$name = "GameScrcpy-$version"
$stage = Join-Path $OutDir $name
Write-Host "Packing $name from $bin"
Reset-Directory $stage
Copy-Release $stage
Assert-NoCommunity $stage
Copy-Item -Path (Join-Path (Find-VcRuntime) '*.dll') -Destination $stage
Assert-Samples $stage @(Add-Samples $stage)
$adb = Get-Adb
$adbFolder = Join-Path $stage 'adb'
New-Item -ItemType Directory -Force $adbFolder | Out-Null
foreach ($file in $adb.Files) { Copy-Item -LiteralPath (Join-Path $adb.Folder $file) -Destination $adbFolder }

Test-Imports $stage
$cli = Invoke-Isolated (Join-Path $stage 'gs.exe') @('--version')
if ($cli.Code -ne 0 -or $cli.Text -notmatch [regex]::Escape($version)) {
    throw "gs.exe --version from the package answered $($cli.Code): $($cli.Text.Trim())"
}
Write-Host "gs.exe --version with only Windows on PATH: $($cli.Text.Trim())"
if ($Smoke) {
    $data = Join-Path ([IO.Path]::GetTempPath()) "gs-package-smoke-$PID"
    try {
        $run = Invoke-Isolated (Join-Path $stage 'GameScrcpy.exe') @('--smoke-test') @{ GS_DATA_DIR = $data } 90
        if ($run.Code -ne 0) { throw "GameScrcpy.exe --smoke-test from the package exited with $($run.Code): $($run.Text.Trim())" }
        Write-Host 'GameScrcpy.exe --smoke-test from the package: first frame shown'
    } finally {
        if (Test-Path -LiteralPath $data) { Remove-Item -LiteralPath $data -Recurse -Force }
    }
}

$outputs = @()
if (-not $NoSetup) {
    $iscc = Find-Iscc
    & $iscc '/Qp' "/DAppVersion=$version" "/DReleaseDir=$stage" "/DOutputDir=$OutDir" "/DAppUrl=$($project.Url)" (Join-Path $root 'packaging\GameScrcpy.iss')
    Assert-ExitCode 'ISCC'
    $outputs += Join-Path $OutDir "$name-setup.exe"
}
$portable = Join-Path $OutDir "$name-portable.zip"
Write-Zip $portable (@(Get-FolderEntries $stage "$name/") + [pscustomobject]@{ File = $null; Name = "$name/portable.flag" })
$outputs += $portable
$symbolEntries = @($Symbols | ForEach-Object { Join-Path $bin $_ } | Where-Object { Test-Path -LiteralPath $_ } |
    ForEach-Object { [pscustomobject]@{ File = $_; Name = Split-Path $_ -Leaf } })
$symbols = Join-Path $OutDir "$name-symbols.zip"
Write-Zip $symbols $symbolEntries
$outputs += $symbols

foreach ($file in $outputs) {
    Write-Host ("{0,8:N1} MB  {1}" -f ((Get-Item -LiteralPath $file).Length / 1MB), $file)
}
