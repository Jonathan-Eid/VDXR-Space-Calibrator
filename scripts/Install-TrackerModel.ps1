param([Parameter(Mandatory=$true)][string]$PackageDirectory)
$ErrorActionPreference='Stop'
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$importer=Join-Path $PSScriptRoot 'Import-DeviceModels.py'
$scratch=Join-Path $package 'data/model-import'
$models=Join-Path $package 'data/models'
New-Item -ItemType Directory -Force $scratch,$models | Out-Null
# Download the manufacturer's own asset for this user's local installation.
# Keep the download and converted model out of public source/release packages.
$url='https://dl.vive.com/Tracker/3D_model/VIVE_Tracker_3.0_3D.zip'
Invoke-WebRequest $url -OutFile "$scratch/official.zip"
Expand-Archive "$scratch/official.zip" "$scratch/official" -Force
Expand-Archive "$scratch/official/VIVE_Tracker_3.0_3D/VIVE_Tracker_3.0_3D_OBJ.zip" "$scratch/obj" -Force
$source=Get-ChildItem "$scratch/obj" -Recurse -Filter 'vr_tracker_vive_3_0.obj' | Select-Object -First 1
if(!$source){throw 'Official archive did not contain the expected model.'}
& python $importer $source.FullName "$models/vive-tracker-3.vdm"
if($LASTEXITCODE -ne 0){throw 'Model import failed. The build-time importer needs Python, numpy and Pillow.'}
@{source=$url;downloaded_utc=[DateTime]::UtcNow.ToString('o');source_sha256=(Get-FileHash $source.FullName -Algorithm SHA256).Hash;output_sha256=(Get-FileHash "$models/vive-tracker-3.vdm" -Algorithm SHA256).Hash} | ConvertTo-Json | Set-Content "$models/vive-tracker-3.provenance.json"
Write-Output "Installed HTC Tracker 3.0 model locally at $models (SteamVR not needed)."
