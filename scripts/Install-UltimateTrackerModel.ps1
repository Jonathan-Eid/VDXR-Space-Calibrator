param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [string]$SourceDirectory
)
$ErrorActionPreference='Stop'
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
if(-not $SourceDirectory) {
    $roots=@($env:ProgramFiles,${env:ProgramFiles(x86)}) | Where-Object {$_}
    $relative=@(
        'VIVE Hub\VIVE Ultimate Tracker\ViveUTOpenVrDriver\vive_ultimate_tracker\resources\rendermodels\vive_ultimate_tracker',
        'VIVE Hub\VIVE Ultimate Tracker\Updater\App\ViveUTOpenVrDriver\vive_ultimate_tracker\resources\rendermodels\vive_ultimate_tracker'
    )
    $candidates=foreach($root in $roots){foreach($path in $relative){Join-Path $root $path}}
    $SourceDirectory=$candidates | Where-Object {Test-Path -LiteralPath (Join-Path $_ 'Vive_Ultimate_Tracker.obj')} | Select-Object -First 1
    if(-not $SourceDirectory){throw 'VIVE Ultimate Tracker model was not found under the standard VIVE Hub installation paths. Pass -SourceDirectory explicitly.'}
}
$source=(Resolve-Path -LiteralPath $SourceDirectory).Path
$obj=Join-Path $source 'Vive_Ultimate_Tracker.obj'
$mtl=Join-Path $source 'Vive_Ultimate_Tracker.mtl'
if(-not (Test-Path -LiteralPath $obj) -or -not (Test-Path -LiteralPath $mtl)){throw 'SourceDirectory must contain Vive_Ultimate_Tracker.obj and Vive_Ultimate_Tracker.mtl.'}
$mapLine=Get-Content -LiteralPath $mtl | Where-Object {$_ -match '^\s*map_Kd\s+'} | Select-Object -First 1
if(-not $mapLine){throw 'The HTC material file has no map_Kd base-color texture.'}
$textureName=($mapLine -replace '^\s*map_Kd\s+','').Trim().Trim('"')
$texture=Join-Path $source $textureName
if(-not (Test-Path -LiteralPath $texture)){throw "The material texture was not found: $textureName"}
$python=Get-Command python -ErrorAction SilentlyContinue
if(-not $python){throw 'Python with numpy and Pillow is required only for local model import.'}
$importer=Join-Path $PSScriptRoot 'Import-DeviceModels.py'
$models=Join-Path $package 'data/models'
New-Item -ItemType Directory -Force -Path $models | Out-Null
$output=Join-Path $models 'vive-ultimate-tracker.vdm'
& $python.Source $importer $obj $output --texture $texture
if($LASTEXITCODE -ne 0){throw 'Ultimate model import failed. Check that Python has numpy and Pillow installed.'}
$receipt=[ordered]@{
    source='VIVE Hub installed VIVE Ultimate Tracker render model'
    source_directory=$source
    imported_utc=[DateTime]::UtcNow.ToString('o')
    files=@(
        [ordered]@{name=[IO.Path]::GetFileName($obj);sha256=(Get-FileHash -LiteralPath $obj -Algorithm SHA256).Hash},
        [ordered]@{name=[IO.Path]::GetFileName($mtl);sha256=(Get-FileHash -LiteralPath $mtl -Algorithm SHA256).Hash},
        [ordered]@{name=[IO.Path]::GetFileName($texture);sha256=(Get-FileHash -LiteralPath $texture -Algorithm SHA256).Hash}
    )
    output='vive-ultimate-tracker.vdm'
    output_sha256=(Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
}
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $models 'vive-ultimate-tracker.provenance.json') -Encoding utf8
Write-Output "Imported VIVE Ultimate Tracker model locally: $output"
