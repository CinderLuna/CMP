param(
    [Parameter(Mandatory)][int]$AppProcessId,
    [ValidateRange(5, 600)][int]$Seconds = 30
)
$process = Get-Process -Id $AppProcessId -ErrorAction Stop
$cpuBefore = $process.TotalProcessorTime.TotalSeconds
$watch = [System.Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Seconds $Seconds
$process.Refresh()
if ($process.HasExited) { throw 'Application exited during measurement.' }
$elapsed = $watch.Elapsed.TotalSeconds
$cpuSeconds = $process.TotalProcessorTime.TotalSeconds - $cpuBefore
[pscustomobject]@{
    ProcessId = $AppProcessId
    ElapsedSeconds = $elapsed
    CpuSeconds = $cpuSeconds
    PercentOfOneLogicalCore = 100 * $cpuSeconds / $elapsed
    WorkingSetMiB = $process.WorkingSet64 / 1MB
    PrivateMemoryMiB = $process.PrivateMemorySize64 / 1MB
    Note = 'Process CPU and memory only. Not power, wakeups, latency, or battery life.'
} | ConvertTo-Json
