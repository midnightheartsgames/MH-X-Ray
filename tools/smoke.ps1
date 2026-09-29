[CmdletBinding()]
param(
    [ValidateSet('r1', 'r2a', 'r2')]
    [string]$Renderer = 'r1',
    [ValidateSet('Win32', 'x64')]
    [string]$Platform = 'Win32',
    [string]$Save = 'smoke_reference',
    [int]$Seconds = 30,
    [int]$TimeoutSeconds = 300,
    [string]$GameDir,
    [string]$Baseline,
    [string]$ExtraArguments,
    [hashtable]$ConfigOverrides = @{},
    [string]$Binaries,
    [string]$SaveAs,
    [string]$JumpTo,
    [switch]$Intro,
    [switch]$Menu,
    [switch]$NewGame
)

$ErrorActionPreference = 'Stop'
$latin1 = [System.Text.Encoding]::GetEncoding(28591)

function Get-WarningLines([string[]]$LogLines) {
    $LogLines | Where-Object { $_.StartsWith('!') } | Sort-Object -Unique
}

if (-not $GameDir) { $GameDir = Join-Path $PSScriptRoot '..\..\S.T.A.L.K.E.R. Shadow of Chernobyl' }
$GameDir = (Resolve-Path $GameDir).Path
if (-not $Binaries) { $Binaries = if ($Platform -eq 'x64') { 'binaries_x64' } else { 'binaries' } }
$runLabel = if ($Platform -eq 'x64') { "x64-$Renderer" } else { $Renderer }
$engine = Join-Path $GameDir "$Binaries\xrEngine.exe"
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
foreach ($key in $ConfigOverrides.Keys) { $overrides[$key] = $ConfigOverrides[$key] }
$userConfig = [System.IO.File]::ReadAllLines((Join-Path $appData 'user.ltx'), $latin1)
$smokeConfig = @($userConfig | Where-Object { $overrides.Keys -notcontains ($_ -split ' ', 2)[0] })
$smokeConfig += $overrides.GetEnumerator() | ForEach-Object { "$($_.Key) $($_.Value)" }
[System.IO.File]::WriteAllLines((Join-Path $appData 'smoke.ltx'), [string[]]$smokeConfig, $latin1)

$arguments = "-silent_error_mode -ltx smoke.ltx"
if (-not $Intro) { $arguments = "-nointro $arguments" }
if ($ExtraArguments) { $arguments += " $ExtraArguments" }
if ($SaveAs) { $arguments += " -smoke_save $SaveAs" }
if ($JumpTo) { $arguments += " -smoke_jump $JumpTo" }
$arguments += " -smoke_test $Seconds"
if ($NewGame) { $arguments += " -start server(all/single/alife/new) client(localhost)" }
elseif (-not $Menu) { $arguments += " -start server($Save/single/alife/load) client(localhost)" }

$saveDir = Join-Path $appData 'savedgames'
$autosaveBackup = $null
if ($JumpTo) {
    $autosaveBackup = Join-Path $resultDir ("autosave-backup-{0:yyyyMMdd-HHmmss}" -f (Get-Date))
    New-Item -ItemType Directory -Force -Path $autosaveBackup | Out-Null
    Get-ChildItem -Path $saveDir -Filter '*_autosave.*' | Copy-Item -Destination $autosaveBackup
}

$startTime = Get-Date
try {
    $process = Start-Process -FilePath $engine -ArgumentList $arguments -WorkingDirectory $GameDir -PassThru
    $null = $process.Handle
    $exited = $process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $exited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
}
finally {
    if ($autosaveBackup) {
        Get-ChildItem -Path $saveDir -Filter '*_autosave.*' | Remove-Item -Force
        Get-ChildItem -Path $autosaveBackup | Copy-Item -Destination $saveDir
        Remove-Item -Path $autosaveBackup -Recurse -Force
    }
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
    $copy = Join-Path $resultDir ("{0:yyyyMMdd-HHmmss}-{1}.log" -f $startTime, $runLabel)
    Copy-Item $log.FullName $copy
}
else {
    $failures.Add('no log written by this run')
}

$fatal = $logLines | Where-Object { $_ -match 'FATAL ERROR|stack trace' }
if ($fatal) { $failures.Add("fatal markers in log: $($fatal | Select-Object -First 3)") }

$smokeLine = $logLines | Where-Object { $_ -like '* smoke_test:*' } | Select-Object -Last 1
if ($log -and -not $smokeLine) { $failures.Add('game did not reach the tested state for the requested time (no smoke_test line)') }

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

Write-Output "platform : $Platform ($Binaries)"
Write-Output "renderer : $Renderer"
Write-Output "target   : $(if ($NewGame) { 'new game' } elseif ($Menu) { 'main menu' } else { $Save })$(if ($JumpTo) { " -> $JumpTo" })"
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
