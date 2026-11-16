param([Parameter(Mandatory=$true)][string]$PackageDirectory)
$ErrorActionPreference='Stop'
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$client=Join-Path $PSScriptRoot 'Inspect-App.ps1'
if (!(Test-Path -LiteralPath $client)) { throw 'Inspection client is missing.' }
$modelSettings=Join-Path $package 'data/device-models.json'
$hadModelSettings=Test-Path -LiteralPath $modelSettings
$savedModelSettings=if($hadModelSettings){[System.IO.File]::ReadAllBytes($modelSettings)}else{$null}
$calibrationSettings=Join-Path $package 'data/calibration-settings.json'
$hadCalibrationSettings=Test-Path -LiteralPath $calibrationSettings
$savedCalibrationSettings=if($hadCalibrationSettings){[System.IO.File]::ReadAllBytes($calibrationSettings)}else{$null}
$testApp=Start-Process -FilePath (Join-Path $package 'vdxr-calibration.exe') -ArgumentList '--preview','--seconds','45' -WorkingDirectory $package -WindowStyle Hidden -PassThru
function Invoke-App([string]$Json) { (& $client -ProcessId $testApp.Id -Request $Json | ConvertFrom-Json) }
try {
    $state=$null
    for($attempt=0;$attempt -lt 30;$attempt++) {
        try {$state=Invoke-App '{"op":"state"}';if($state.mode -eq 'synthetic_preview'){break}} catch { }
        Start-Sleep -Milliseconds 100
    }
    if($state.mode -ne 'synthetic_preview'){throw 'Preview did not initialize.'}
    if(!(Invoke-App '{"op":"set_fast_mode","value":0}').ok){throw 'Slow capture selection failed.'}
    $slow=Invoke-App '{"op":"state"}'
    if($slow.fast_mode -or $slow.capture_target_samples -ne 250){throw 'Slow sample target did not change.'}
    $null=Invoke-App '{"op":"exit"}'
    if(!$testApp.WaitForExit(5000)){throw 'Preview did not exit before persistence check.'}
    $testApp=Start-Process -FilePath (Join-Path $package 'vdxr-calibration.exe') -ArgumentList '--preview','--seconds','45' -WorkingDirectory $package -WindowStyle Hidden -PassThru
    $slow=$null
    for($attempt=0;$attempt -lt 30;$attempt++){try{$slow=Invoke-App '{"op":"state"}';if($slow.mode -eq 'synthetic_preview'){break}}catch{};Start-Sleep -Milliseconds 100}
    if($slow.mode -ne 'synthetic_preview' -or $slow.fast_mode -or $slow.capture_target_samples -ne 250){throw 'Capture speed did not persist across restart.'}
    if(!(Invoke-App '{"op":"set_fast_mode","value":1}').ok){throw 'Fast capture selection failed.'}
    $fast=Invoke-App '{"op":"state"}'
    if(!$fast.fast_mode -or $fast.capture_target_samples -ne 100){throw 'Fast sample target did not change.'}
    if((Invoke-App '{"op":"set_fast_mode","value":2}').ok){throw 'Invalid fast mode value accepted.'}
    if(!(Invoke-App '{"op":"select_reference","value":1}').ok){throw 'Selection command failed.'}
    if((Invoke-App '{"op":"state"}').reference -ne 'right'){throw 'Selection was not reflected in state.'}
    if((Invoke-App '{"op":"select_reference","value":7}').ok){throw 'Invalid selection was accepted.'}
    if((Invoke-App '{"op":"save_profile"}').ok){throw 'Preview allowed saving a profile.'}
    if((Invoke-App '{"op":"start_capture"}').ok){throw 'Preview allowed hardware capture.'}
    if(!(Invoke-App '{"op":"select_left_model","value":"oculus-touch-v3"}').ok){throw 'Quest 2 model selection failed.'}
    if(!(Invoke-App '{"op":"select_right_model","value":"meta-quest-touch-plus"}').ok){throw 'Quest 3 model selection failed.'}
    if((Invoke-App '{"op":"select_left_model","value":"../../invalid"}').ok){throw 'Invalid model was accepted.'}
    $modelState=(Invoke-App '{"op":"state"}').device_models
    $left=@($modelState | Where-Object device -eq 'left_controller')[0]
    $right=@($modelState | Where-Object device -eq 'right_controller')[0]
    if($left.model_id -ne 'oculus-touch-v3' -or !$left.asset_loaded -or $left.identity_source -ne 'manual override'){throw 'Quest 2 identity/mesh missing.'}
    if($right.model_id -ne 'meta-quest-touch-plus' -or !$right.asset_loaded){throw 'Quest 3 identity/mesh missing.'}
    if(!(Invoke-App '{"op":"show_device_models","value":1}').ok -or !(Invoke-App '{"op":"state"}').device_models_open){throw 'Device model view did not open.'}
    if(!(Invoke-App '{"op":"show_device_models","value":0}').ok -or (Invoke-App '{"op":"state"}').device_models_open){throw 'Device model view did not close.'}
    $export=Invoke-App '{"op":"export_frame","view":"mirror"}'
    if(!$export.ok){throw 'Frame export was rejected.'}
    for($attempt=0;$attempt -lt 30;$attempt++) {
        $state=Invoke-App '{"op":"state"}'
        if($state.render.export_completed -ge $export.export_ticket){break}
        Start-Sleep -Milliseconds 100
    }
    if($state.render.export_completed -lt $export.export_ticket){throw 'Export did not complete.'}
    Add-Type -AssemblyName System.Drawing
    $bitmap=[System.Drawing.Bitmap]::new((Join-Path $package $state.render.last_export))
    try {
        if($bitmap.Width -ne 1280 -or $bitmap.Height -ne 840){throw 'Export dimensions are incorrect.'}
        $color=$bitmap.GetPixel(1260,30)
        if($color.B -le $color.R -or $color.A -ne 255){throw 'PNG color channels or opacity are incorrect.'}
    } finally {$bitmap.Dispose()}
    $null=Invoke-App '{"op":"exit"}'
    if(!$testApp.WaitForExit(5000)){throw 'App did not exit through its API.'}
    'Passed: native state, selection, Quest 2/3 meshes, invalid inputs, preview gates, PNG export and API shutdown.'
} finally {
    if(!$testApp.HasExited){Stop-Process -Id $testApp.Id}
    if($hadModelSettings){[System.IO.File]::WriteAllBytes($modelSettings,$savedModelSettings)}elseif(Test-Path -LiteralPath $modelSettings){Remove-Item -LiteralPath $modelSettings}
    if($hadCalibrationSettings){[System.IO.File]::WriteAllBytes($calibrationSettings,$savedCalibrationSettings)}elseif(Test-Path -LiteralPath $calibrationSettings){Remove-Item -LiteralPath $calibrationSettings}
}
