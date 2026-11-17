param([string]$PackageDirectory=(Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference='Stop'
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$app=Join-Path $package 'vdxr-calibration.exe'
if(-not (Test-Path -LiteralPath $app -PathType Leaf)){throw 'PackageDirectory must contain the built calibration app.'}
# Use this as the child application of a Nightfall per-app bootstrap profile.
# Nightfall supplies the bridge/session environment; do not overwrite it here.
$env:DISABLE_XR_APILAYER_VIVE_HAND_TRACKING_1='1'
# Calibration needs normal Quest head/controller actions before tracker roles
# are configured. The app explicitly opens its Ultimate source itself.
$env:VDXR_TRACKER_SOURCE=$null
Set-Location -LiteralPath $package
& $app --tracker-source ultimate
exit $LASTEXITCODE
