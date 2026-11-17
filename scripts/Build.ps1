param(
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory,
    [switch]$UltimateOnly,
    [string]$UltimateSourceDirectory,
    [string]$UltimateModelSourceDirectory,
    [string]$EigenIncludeDirectory,
    [string]$Generator='Visual Studio 17 2022'
)
$ErrorActionPreference='Stop'
function Invoke-Checked([string]$Program,[string[]]$Arguments) {
    & $Program @Arguments
    if($LASTEXITCODE -ne 0){throw "$Program failed with exit code $LASTEXITCODE"}
}
$repo=(Resolve-Path -LiteralPath "$PSScriptRoot/..").Path
$runtime=(Resolve-Path -LiteralPath $RuntimeDirectory).Path
New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
$build=(Resolve-Path -LiteralPath $BuildRoot).Path
if($UltimateOnly) {
    Invoke-Checked git @('-C',$repo,'submodule','update','--init','external/OpenXR-SDK','external/cJSON','external/imgui')
}
else { Invoke-Checked git @('-C',$repo,'submodule','update','--init','--recursive') }
$surviveSource=Join-Path $repo 'external/libsurvive'
$appBuild=Join-Path $build 'app-build'
if($UltimateOnly) {
    if(-not $UltimateSourceDirectory){throw 'UltimateOnly requires -UltimateSourceDirectory pointing to VDXR tools/ultimate-trackers.'}
    if(-not $EigenIncludeDirectory){throw 'UltimateOnly requires -EigenIncludeDirectory pointing to an Eigen include root.'}
    $ultimateSource=(Resolve-Path -LiteralPath $UltimateSourceDirectory).Path
    $eigenInclude=(Resolve-Path -LiteralPath $EigenIncludeDirectory).Path
    if(-not (Test-Path -LiteralPath (Join-Path $ultimateSource 'ultimate_hid_source.cpp'))){throw 'UltimateSourceDirectory does not contain ultimate_hid_source.cpp.'}
    Invoke-Checked cmake @('-S',$repo,'-B',$appBuild,'-G',$Generator,'-A','x64','-DVDXR_ULTIMATE_ONLY=ON',"-DVDXR_ULTIMATE_SOURCE_DIR=$ultimateSource","-DEIGEN_INCLUDE_DIR=$eigenInclude")
}
else {
    $importPatch=Join-Path $repo 'patches/libsurvive-windows-steamvr-import.patch'
    & git -C $surviveSource apply --check $importPatch 2>$null
    if($LASTEXITCODE -eq 0){Invoke-Checked git @('-C',$surviveSource,'apply',$importPatch)}
    else {
        & git -C $surviveSource apply --reverse --check $importPatch 2>$null
        if($LASTEXITCODE -ne 0){throw 'Pinned libsurvive does not match the tested Windows database-import patch.'}
    }
    $surviveBuild=Join-Path $build 'libsurvive-build'
    Invoke-Checked cmake @('-S',$surviveSource,'-B',$surviveBuild,'-G',$Generator,'-A','x64','-DUSE_SINGLE_PRECISION=OFF','-DENABLE_TESTS=OFF')
    Invoke-Checked cmake @('--build',$surviveBuild,'--config','Release','--target','survive-cli','--parallel','4')
    Invoke-Checked cmake @('-S',$repo,'-B',$appBuild,'-G',$Generator,'-A','x64',"-DSURVIVE_SOURCE=$surviveSource", "-DSURVIVE_LIBRARY=$surviveBuild/src/Release/survive.lib", "-DEIGEN_INCLUDE_DIR=$surviveBuild/libs/cnmatrix/libeigen-src")
}
Invoke-Checked cmake @('--build',$appBuild,'--config','Release','--parallel','2')
Invoke-Checked ctest @('--test-dir',$appBuild,'-C','Release','--output-on-failure')
$package=Join-Path $build 'package'
foreach($folder in @($package,"$package/runtime","$package/plugins","$package/data","$package/licenses")){New-Item -ItemType Directory -Force -Path $folder | Out-Null}
Copy-Item -LiteralPath "$appBuild/Release/vdxr-calibration.exe","$appBuild/Release/calibration-replay.exe" -Destination $package
if(-not $UltimateOnly) {
    Copy-Item -LiteralPath "$surviveBuild/Release/libsurvive.dll" -Destination $package
    Copy-Item "$surviveBuild/Release/plugins/*.dll" -Destination "$package/plugins"
}
Copy-Item -LiteralPath "$runtime/virtualdesktop-openxr.dll","$runtime/virtualdesktop-openxr.json","$runtime/LibOVRRT64_1.dll" -Destination "$package/runtime"
Copy-Item -LiteralPath "$repo/README.md","$repo/LICENSE","$repo/NOTICE","$PSScriptRoot/Inspect-App.ps1","$PSScriptRoot/Test-AppInspection.ps1" -Destination $package
New-Item -ItemType Directory -Force "$package/docs" | Out-Null
Copy-Item -LiteralPath "$repo/docs/CALIBRATION_REFERENCES.md" -Destination "$package/docs/CALIBRATION_REFERENCES.md"
Copy-Item -LiteralPath "$repo/assets" -Destination $package -Recurse -Force
Copy-Item -LiteralPath "$repo/licenses/SpaceCalibrator-MIT.txt" -Destination "$package/licenses/SpaceCalibrator-MIT.txt"
Copy-Item -LiteralPath "$repo/assets/models/LICENSE-WebXR.md" -Destination "$package/licenses/WebXR-Input-Profiles.txt"
New-Item -ItemType Directory -Force "$package/scripts" | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/Install-TrackerModel.ps1","$PSScriptRoot/Install-UltimateTrackerModel.ps1","$PSScriptRoot/Start-UltimateCalibration.ps1","$PSScriptRoot/Import-DeviceModels.py" -Destination "$package/scripts"
Copy-Item -LiteralPath "$repo/external/imgui/LICENSE.txt" -Destination "$package/licenses/ImGui.txt"
if(-not $UltimateOnly) {
    Copy-Item -LiteralPath "$repo/external/libsurvive/LICENSE" -Destination "$package/licenses/libsurvive.txt"
    Copy-Item -LiteralPath "$repo/external/libsurvive/libs/cnmatrix/LICENSE" -Destination "$package/licenses/cnmatrix.txt"
    Copy-Item -LiteralPath "$repo/external/libsurvive/libs/cnkalman/LICENSE" -Destination "$package/licenses/cnkalman.txt"
}
Copy-Item -LiteralPath "$repo/external/cJSON/LICENSE" -Destination "$package/licenses/cJSON.txt"
if($UltimateOnly){Copy-Item -LiteralPath "$eigenInclude/COPYING.MPL2" -Destination "$package/licenses/Eigen-MPL2.txt"}
else{Copy-Item -LiteralPath "$surviveBuild/libs/cnmatrix/libeigen-src/COPYING.MPL2" -Destination "$package/licenses/Eigen-MPL2.txt"}
if(Test-Path -LiteralPath "$runtime/../../LICENSE"){Copy-Item -LiteralPath "$runtime/../../LICENSE" -Destination "$package/licenses/VDXR.txt"}
if($UltimateModelSourceDirectory){& "$PSScriptRoot/Install-UltimateTrackerModel.ps1" -PackageDirectory $package -SourceDirectory $UltimateModelSourceDirectory}
if($UltimateOnly){& "$PSScriptRoot/Test-AppInspection.ps1" -PackageDirectory $package -TrackerSource ultimate}
else{& "$PSScriptRoot/Test-AppInspection.ps1" -PackageDirectory $package -TrackerSource lighthouse}
Write-Output "App package: $package"
