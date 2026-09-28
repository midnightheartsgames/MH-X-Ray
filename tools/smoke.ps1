[CmdletBinding()]
param(
    [ValidateSet('r1', 'r2a', 'r2')]
    [string]$Renderer = 'r1',
    [string]$Save = 'smoke_reference',
    [int]$Seconds = 30,
    [int]$TimeoutSeconds = 300,
    [string]$GameDir = (Join-Path $PSScriptRoot '..\..\S.T.A.L.K.E.R. Shadow of Chernobyl'),
    [string]$Baseline
)

$ErrorActionPreference = 'Stop'
$latin1 = [System.Text.Encoding]::GetEncoding(28591)

function Get-WarningLines([string[]]$LogLines) {
    $LogLines | Where-Object { $_.StartsWith('!') } | Sort-Object -Unique
}

$GameDir = (Resolve-Path $GameDir).Path
$engine = Join-Path $GameDir 'binaries\xrEngine.exe'
$appData = Join-Path $GameDir 'appdata'
$logDir = Join-Path $appData 'logs'
$resultDir = Join-Path $PSScriptRoot '..\Output\Smoke'

if (-not (Test-Path $engine)) { throw "Engine not found: $engine" }
New-Item -ItemType Directory -Force -Path $resultDir | Out-Null

$overrides = [ordered]@{
    'renderer'         = "renderer_$Renderer"
    'rs_fullscreen'    = 'off'
    'rs_borderless'    = 'off'
    'vid_mode'         = '1280x720'
    'snd_volume_eff'   = '0'
    'snd_volume_music' = '0'
}
$userConfig = [System.IO.File]::ReadAllLines((Join-Path $appData 'user.ltx'), $latin1)
$smokeConfig = @($userConfig | Where-Object { $overrides.Keys -notcontains ($_ -split ' ', 2)[0] })
$smokeConfig += $overrides.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" }
[System.IO.File]::WriteAllLines((Join-Path $appData 'smoke.ltx'), [string[]]$smokeConfig, $latin1)

$arguments = "-nointro -silent_error_mode -ltx smoke.ltx -smoke_test $Seconds -start server($Save/single/alife/load) client(localhost)"
$startTime = Get-Date
$process = Start-Process -FilePath $engine -ArgumentList $arguments -WorkingDirectory $GameDir -PassThru
$null = $process.Handle
$exited = $process.WaitForExit($TimeoutSeconds * 1000)
if (-not $exited) {
    Stop-Process -Id $process.Id -Force
    $process.WaitForExit()
}
$elapsed = [int]((Get-Date) - $startTime).TotalSeconds

$failures = [System.Collections.Generic.List[string]]::new()
if (-not $exited) { $failures.Add("timeout after $TimeoutSeconds s, process killed") }

$log = Get-ChildItem -Path $logDir -Filter 'xray_*.log' |
    Where-Object { $_.LastWriteTime -ge $startTime } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

$logLines = @()
if ($log) {
    $logLines = [System.IO.File]::ReadAllLines($log.FullName, $latin1)
    $copy = Join-Path $resultDir ("{0:yyyyMMdd-HHmmss}-{1}.log" -f $startTime, $Renderer)
    Copy-Item $log.FullName $copy
}
else {
    $failures.Add('no log written by this run')
}

$fatal = $logLines | Where-Object { $_ -match 'FATAL ERROR|stack trace' }
if ($fatal) { $failures.Add("fatal markers in log: $($fatal | Select-Object -First 3)") }

$smokeLine = $logLines | Where-Object { $_ -like '* smoke_test:*' } | Select-Object -Last 1
if ($log -and -not $smokeLine) { $failures.Add('level did not load and run for the requested time (no smoke_test line)') }

$fps = '-'
if ($smokeLine -match 'smoke_test: \d+ frames in \d+ ms, ([\d.]+) fps, (\d+) inactive frames') {
    $fps = if ([int]$Matches[2] -eq 0) { $Matches[1] } else { "invalid, window inactive for $($Matches[2]) frames" }
}

$warnings = @(Get-WarningLines $logLines)
$newWarnings = @()
if ($Baseline) {
    $baselineWarnings = @(Get-WarningLines ([System.IO.File]::ReadAllLines((Resolve-Path $Baseline).Path, $latin1)))
    $newWarnings = @($warnings | Where-Object { $baselineWarnings -notcontains $_ })
    if ($newWarnings.Count) { $failures.Add("$($newWarnings.Count) warning line(s) not present in baseline") }
}

$crashReports = Get-ChildItem -Path $logDir |
    Where-Object { $_.Name -notlike 'xray_*.log' -and $_.LastWriteTime -ge $startTime }

Write-Output "renderer : $Renderer"
Write-Output "save     : $Save"
Write-Output "exit     : $(if ($exited) { $process.ExitCode } else { 'killed' })"
Write-Output "elapsed  : $elapsed s"
Write-Output "fps      : $fps"
Write-Output "warnings : $($warnings.Count) distinct"
if ($copy) { Write-Output "log      : $((Resolve-Path $copy).Path)" }
foreach ($report in $crashReports) { Write-Output "report   : $($report.FullName)" }
foreach ($line in $newWarnings) { Write-Output "new      : $line" }

if ($failures.Count) {
    foreach ($failure in $failures) { Write-Output "FAIL     : $failure" }
    exit 1
}
Write-Output 'PASS'
