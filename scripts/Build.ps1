param(
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [Parameter(Mandatory=$true)][string]$RuntimeDirectory
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
Invoke-Checked git @('-C',$repo,'submodule','update','--init','--recursive')
$surviveSource=Join-Path $repo 'external/libsurvive'
$importPatch=Join-Path $repo 'patches/libsurvive-windows-steamvr-import.patch'
& git -C $surviveSource apply --check $importPatch 2>$null
if($LASTEXITCODE -eq 0){Invoke-Checked git @('-C',$surviveSource,'apply',$importPatch)}
else {
    & git -C $surviveSource apply --reverse --check $importPatch 2>$null
    if($LASTEXITCODE -ne 0){throw 'Pinned libsurvive does not match the tested Windows database-import patch.'}
}
$surviveBuild=Join-Path $build 'libsurvive-build'
$appBuild=Join-Path $build 'app-build'
Invoke-Checked cmake @('-S',$surviveSource,'-B',$surviveBuild,'-G','Visual Studio 17 2022','-A','x64','-DUSE_SINGLE_PRECISION=OFF','-DENABLE_TESTS=OFF')
Invoke-Checked cmake @('--build',$surviveBuild,'--config','Release','--target','survive-cli','--parallel','4')
Invoke-Checked cmake @('-S',$repo,'-B',$appBuild,'-G','Visual Studio 17 2022','-A','x64',"-DSURVIVE_SOURCE=$surviveSource", "-DSURVIVE_LIBRARY=$surviveBuild/src/Release/survive.lib", "-DEIGEN_INCLUDE_DIR=$surviveBuild/libs/cnmatrix/libeigen-src")
Invoke-Checked cmake @('--build',$appBuild,'--config','Release','--parallel','2')
Invoke-Checked ctest @('--test-dir',$appBuild,'-C','Release','--output-on-failure')
$package=Join-Path $build 'package'
foreach($folder in @($package,"$package/runtime","$package/plugins","$package/data","$package/licenses")){New-Item -ItemType Directory -Force -Path $folder | Out-Null}
Copy-Item -LiteralPath "$appBuild/Release/vdxr-calibration.exe","$appBuild/Release/calibration-replay.exe","$surviveBuild/Release/libsurvive.dll" -Destination $package
Copy-Item "$surviveBuild/Release/plugins/*.dll" -Destination "$package/plugins"
Copy-Item -LiteralPath "$runtime/virtualdesktop-openxr.dll","$runtime/virtualdesktop-openxr.json" -Destination "$package/runtime"
Copy-Item -LiteralPath "$repo/README.md","$repo/LICENSE","$repo/NOTICE","$PSScriptRoot/Inspect-App.ps1","$PSScriptRoot/Test-AppInspection.ps1" -Destination $package
Copy-Item -LiteralPath "$repo/assets" -Destination $package -Recurse -Force
Copy-Item -LiteralPath "$repo/licenses/SpaceCalibrator-MIT.txt" -Destination "$package/licenses/SpaceCalibrator-MIT.txt"
Copy-Item -LiteralPath "$repo/assets/models/LICENSE-WebXR.md" -Destination "$package/licenses/WebXR-Input-Profiles.txt"
New-Item -ItemType Directory -Force "$package/scripts" | Out-Null
Copy-Item -LiteralPath "$PSScriptRoot/Install-TrackerModel.ps1","$PSScriptRoot/Import-DeviceModels.py" -Destination "$package/scripts"
Copy-Item -LiteralPath "$repo/external/imgui/LICENSE.txt" -Destination "$package/licenses/ImGui.txt"
Copy-Item -LiteralPath "$repo/external/libsurvive/LICENSE" -Destination "$package/licenses/libsurvive.txt"
Copy-Item -LiteralPath "$repo/external/libsurvive/libs/cnmatrix/LICENSE" -Destination "$package/licenses/cnmatrix.txt"
Copy-Item -LiteralPath "$repo/external/libsurvive/libs/cnkalman/LICENSE" -Destination "$package/licenses/cnkalman.txt"
Copy-Item -LiteralPath "$repo/external/cJSON/LICENSE" -Destination "$package/licenses/cJSON.txt"
Copy-Item -LiteralPath "$surviveBuild/libs/cnmatrix/libeigen-src/COPYING.MPL2" -Destination "$package/licenses/Eigen-MPL2.txt"
if(Test-Path -LiteralPath "$runtime/../../LICENSE"){Copy-Item -LiteralPath "$runtime/../../LICENSE" -Destination "$package/licenses/VDXR.txt"}
& "$PSScriptRoot/Test-AppInspection.ps1" -PackageDirectory $package
Write-Output "App package: $package"
