param(
    [Parameter(Mandatory=$true,ParameterSetName='Images')][string[]]$Image,
    [Parameter(Mandatory=$true,ParameterSetName='Rows')][switch]$Rows,
    [ValidateSet(0,90,180,270)][int]$Rotate = 90,
    [ValidateRange(0.1,86400)][double]$Seconds = 10,
    [ValidateRange(1,32)][int]$RowStep = 4,
    [ValidateSet('white','red','green','blue')][string]$RowColor = 'white',
    [ValidateRange(0,86400)][double]$FrameSeconds = 0
)
$ErrorActionPreference = 'Stop'
$imagePaths = @()
if (-not $Rows) {
    $imagePaths = @($Image | ForEach-Object { (Resolve-Path -LiteralPath $_).Path })
}
if ($Rows -and -not $PSBoundParameters.ContainsKey('Seconds')) { $Seconds = 1 }
if ($Rows -and -not $PSBoundParameters.ContainsKey('Rotate')) { $Rotate = 0 }
$generated = Join-Path $PSScriptRoot 'generated'
New-Item -ItemType Directory -Force $generated | Out-Null
$statePath = Join-Path $generated 'video-process.json'
if (Test-Path -LiteralPath $statePath) {
    $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    $existing = Get-CimInstance Win32_Process -Filter "ProcessId = $($state.pid)"
    if ($existing -and $existing.CommandLine -like '*usb_video.py*') {
        throw 'Video sender is already running. Run stop_video.ps1 first.'
    }
}
$stopPath = Join-Path $generated 'video.stop'
if (Test-Path -LiteralPath $stopPath) { Remove-Item -LiteralPath $stopPath }
$python = (Get-Command python).Source
$arguments = @('-u', ('"' + (Join-Path $PSScriptRoot 'usb_video.py') + '"'))
if ($imagePaths.Count -gt 1) { $arguments += '--playlist' }
if ($Rows) { $arguments += @('--rows', '--row-step', $RowStep, '--row-color', $RowColor) }
$arguments += @($imagePaths | ForEach-Object { '"' + $_ + '"' })
if ($FrameSeconds -gt 0) {
    $arguments += @('--frame-seconds', $FrameSeconds.ToString([Globalization.CultureInfo]::InvariantCulture))
}
$arguments += @('--seconds', $Seconds.ToString([Globalization.CultureInfo]::InvariantCulture),
    '--rotate', $Rotate, '--loops', '0',
    '--stop-file', ('"' + $stopPath + '"'))
$process = Start-Process -FilePath $python -ArgumentList $arguments -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $generated 'video-live.log') `
    -RedirectStandardError (Join-Path $generated 'video-error.log')
@{pid=$process.Id; images=$imagePaths; rows=[bool]$Rows; rowStep=$RowStep; rowColor=$RowColor; rotate=$Rotate; seconds=$Seconds; frameSeconds=$FrameSeconds; started=(Get-Date).ToString('o')} |
    ConvertTo-Json | Set-Content -LiteralPath $statePath
Write-Output "Video sender started, PID $($process.Id). Stop with pico/stop_video.ps1."
