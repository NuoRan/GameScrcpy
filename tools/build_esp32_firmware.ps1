param(
    [string]$IdfRoot = $(if ($env:GS_IDF_ROOT) { $env:GS_IDF_ROOT } else { 'C:\Espressif' }),
    [string]$IdfPath = '',
    [string[]]$Targets = @('esp32s3', 'esp32s2', 'esp32p4', 'esp32', 'esp32c3', 'esp32c6'),
    [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'

$repository = Split-Path -Parent $PSScriptRoot
$project = Join-Path $repository 'firmware\esp32'
$release = Join-Path $project 'release'
$buildRoot = Join-Path $repository 'build\esp32-firmware'
$manifestPath = Join-Path $release 'firmware.json'
$chipIds = @{ esp32 = 0; esp32s2 = 2; esp32c3 = 5; esp32s3 = 9; esp32c6 = 13; esp32p4 = 18 }
$flashSize = '4MB'

function Find-EspIdf {
    if ($IdfPath -and (Test-Path (Join-Path $IdfPath 'export.ps1'))) {
        return $IdfPath
    }
    if ($env:IDF_PATH -and (Test-Path (Join-Path $env:IDF_PATH 'export.ps1'))) {
        return $env:IDF_PATH
    }
    $candidates = @(
        (Join-Path $IdfRoot 'v6.1\esp-idf'),
        (Join-Path $IdfRoot 'frameworks\esp-idf-v6.1'),
        (Join-Path $IdfRoot 'esp-idf-v6.1'),
        (Join-Path $IdfRoot 'esp-idf')
    )
    foreach ($candidate in $candidates) {
        if (Test-Path (Join-Path $candidate 'export.ps1')) {
            return $candidate
        }
    }
    return $null
}

foreach ($target in $Targets) {
    if (-not $chipIds.ContainsKey($target)) {
        throw "Unknown target '$target'. Known targets: $($chipIds.Keys -join ', ')"
    }
}

$idf = Find-EspIdf
if (-not $idf) {
    Write-Host "ESP-IDF v6.1 was not found under $IdfRoot."
    Write-Host 'Install it once, for example:'
    Write-Host "  git clone -b v6.1 --recursive https://github.com/espressif/esp-idf.git $IdfRoot\esp-idf"
    Write-Host "  `$env:IDF_TOOLS_PATH = '$IdfRoot\tools'"
    Write-Host "  $IdfRoot\esp-idf\install.ps1 esp32,esp32s2,esp32s3,esp32c3,esp32c6,esp32p4"
    Write-Host "or with the ESP-IDF Installation Manager (eim), choosing v6.1 and the folder $IdfRoot."
    Write-Host 'Then run this script again, or pass -IdfPath <the folder that holds export.ps1>.'
    exit 1
}

if (-not $env:IDF_TOOLS_PATH -and (Test-Path (Join-Path $IdfRoot 'tools'))) {
    $env:IDF_TOOLS_PATH = Join-Path $IdfRoot 'tools'
}

. (Join-Path $idf 'export.ps1')

$idfVersion = (& idf.py --version) -join ' '
if ($idfVersion -notmatch 'v6\.1') {
    Write-Warning "Expected ESP-IDF v6.1, found: $idfVersion"
}

$version = (Get-Content (Join-Path $project 'version.txt') -Raw).Trim()
New-Item -ItemType Directory -Force -Path $release | Out-Null
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

$entries = [ordered]@{}
if (Test-Path $manifestPath) {
    $existing = Get-Content $manifestPath -Raw | ConvertFrom-Json
    if ($existing.version -eq $version -and $existing.targets) {
        foreach ($property in $existing.targets.PSObject.Properties) {
            if ($Targets -notcontains $property.Name -and (Test-Path (Join-Path $release $property.Value.file))) {
                $entries[$property.Name] = [ordered]@{
                    file = $property.Value.file
                    sha256 = $property.Value.sha256
                    flashSize = $property.Value.flashSize
                    chipId = [int]$property.Value.chipId
                }
            }
        }
    }
}

foreach ($target in $Targets) {
    $build = Join-Path $buildRoot $target
    $sdkconfig = Join-Path $build 'sdkconfig'
    $image = "gs-esp32-$target.bin"
    $output = Join-Path $release $image
    Write-Host "==> $target"
    & cmake -G Ninja -S $project -B $build "-DIDF_TARGET=$target" "-DSDKCONFIG=$sdkconfig"
    if ($LASTEXITCODE -ne 0) {
        throw "Configuring $target failed"
    }
    & cmake --build $build --parallel $Jobs
    if ($LASTEXITCODE -ne 0) {
        throw "Building $target failed"
    }
    Push-Location $build
    try {
        & python -m esptool --chip $target merge_bin -o $output '@flash_args'
        if ($LASTEXITCODE -ne 0) {
            & idf.py -C $project -B $build merge-bin -o $output
        }
    } finally {
        Pop-Location
    }
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $output)) {
        throw "Merging the $target image failed"
    }
    $entries[$target] = [ordered]@{
        file = $image
        sha256 = (Get-FileHash -Algorithm SHA256 $output).Hash.ToLowerInvariant()
        flashSize = $flashSize
        chipId = $chipIds[$target]
    }
}

$manifest = [ordered]@{
    format = 'gamescrcpy.esp32-firmware'
    version = $version
    targets = $entries
}
$json = $manifest | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText($manifestPath, $json + "`n", (New-Object System.Text.UTF8Encoding $false))
Write-Host "Wrote $manifestPath with $($entries.Count) image(s), firmware $version"
