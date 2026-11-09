param(
    [Parameter(Mandatory)][string]$RootDirectory,
    [Parameter(Mandatory)][string]$Token,
    [Parameter(Mandatory)][string]$ArgumentProbe
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RootDirectory)
$marker = Join-Path $root '.managed-service-test'
if (-not (Test-Path -LiteralPath $marker -PathType Leaf) -or
    (Get-Content -LiteralPath $marker -Raw).Trim() -cne $Token -or
    $Token -cnotmatch '^[a-f0-9]{32}$') {
    throw 'This fixture requires an explicitly prepared isolated test directory.'
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$currentProcess = [Diagnostics.Process]::GetCurrentProcess()
$state = @($identity, [Environment]::CurrentDirectory, $ArgumentProbe, "$PID",
    "ENV_BASE=$env:NSSM_NEXT_BASE", "ENV_PROBE=$env:NSSM_NEXT_PROBE", "ENV_EMPTY=$env:NSSM_NEXT_EMPTY",
    "PRIORITY=$($currentProcess.PriorityClass)", "AFFINITY=$($currentProcess.ProcessorAffinity.ToInt64())")
$pending = Join-Path $root "started-$PID.pending"
$ready = Join-Path $root "started-$PID.txt"
$state | Set-Content -LiteralPath $pending -Encoding Unicode
Move-Item -LiteralPath $pending -Destination $ready
[Console]::Out.WriteLine('NssmNext stdout probe')
[Console]::Error.WriteLine('NssmNext stderr probe')
while ($true) {
    $exitFile = Join-Path $root 'exit-code.txt'
    if (Test-Path -LiteralPath $exitFile -PathType Leaf) {
        $code = [int](Get-Content -LiteralPath $exitFile -Raw)
        Remove-Item -LiteralPath $exitFile
        exit $code
    }
    Start-Sleep -Milliseconds 250
}
