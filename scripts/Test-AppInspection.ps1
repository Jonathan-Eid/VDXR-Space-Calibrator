param([Parameter(Mandatory=$true)][string]$PackageDirectory)
$ErrorActionPreference='Stop'
$package=(Resolve-Path -LiteralPath $PackageDirectory).Path
$client=Join-Path $PSScriptRoot 'Inspect-App.ps1'
if (!(Test-Path -LiteralPath $client)) { throw 'Inspection client is missing.' }
$testApp=Start-Process -FilePath (Join-Path $package 'vdxr-calibration.exe') -ArgumentList '--preview','--seconds','45' -WorkingDirectory $package -WindowStyle Hidden -PassThru
function Invoke-App([string]$Json) { (& $client -ProcessId $testApp.Id -Request $Json | ConvertFrom-Json) }
try {
    $state=$null
    for($attempt=0;$attempt -lt 30;$attempt++) {
        try {$state=Invoke-App '{"op":"state"}';if($state.mode -eq 'synthetic_preview'){break}} catch { }
        Start-Sleep -Milliseconds 100
    }
    if($state.mode -ne 'synthetic_preview'){throw 'Preview did not initialize.'}
    if(!(Invoke-App '{"op":"select_reference","value":1}').ok){throw 'Selection command failed.'}
    if((Invoke-App '{"op":"state"}').reference -ne 'right'){throw 'Selection was not reflected in state.'}
    if((Invoke-App '{"op":"select_reference","value":7}').ok){throw 'Invalid selection was accepted.'}
    if((Invoke-App '{"op":"save_profile"}').ok){throw 'Preview allowed saving a profile.'}
    if((Invoke-App '{"op":"start_capture"}').ok){throw 'Preview allowed hardware capture.'}
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
    'Passed: native state, shared selection action, invalid input, preview gates, PNG export and API shutdown.'
} finally {
    if(!$testApp.HasExited){Stop-Process -Id $testApp.Id}
}
