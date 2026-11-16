param(
    [int]$ProcessId,
    [string]$Request = '{"op":"state"}',
    [ValidateRange(100,10000)][int]$TimeoutMs = 7000
)
$ErrorActionPreference='Stop'
if (!$ProcessId) {
    $apps=@(Get-Process -Name vdxr-calibration -ErrorAction SilentlyContinue)
    if ($apps.Count -ne 1) { throw 'Specify -ProcessId for one running calibration app.' }
    $ProcessId=$apps[0].Id
}
$null=$Request | ConvertFrom-Json
$client=[System.IO.Pipes.NamedPipeClientStream]::new('.',"VDXRCalibration.$ProcessId",[System.IO.Pipes.PipeDirection]::InOut)
try {
    $client.Connect($TimeoutMs)
    $client.ReadMode=[System.IO.Pipes.PipeTransmissionMode]::Message
    $bytes=[System.Text.Encoding]::UTF8.GetBytes($Request)
    $client.Write($bytes,0,$bytes.Length)
    $result=[System.IO.MemoryStream]::new()
    do {
        $buffer=[byte[]]::new(65536)
        $read=$client.ReadAsync($buffer,0,$buffer.Length)
        if (!$read.Wait($TimeoutMs)) { throw 'Inspection request timed out. Inspect state before retrying a command.' }
        $count=$read.Result
        if (!$count) { throw 'Inspection connection closed before a response.' }
        $result.Write($buffer,0,$count)
    } while (!$client.IsMessageComplete)
    $ack=[System.Text.Encoding]::UTF8.GetBytes('{"ack":true}')
    $client.Write($ack,0,$ack.Length)
    [System.Text.Encoding]::UTF8.GetString($result.ToArray())
} finally { $client.Dispose() }
