param([Parameter(Mandatory = $true)][string]$StateDirectory)

$ErrorActionPreference = 'Stop'
$root = $env:NSSM_NEXT_SMOKE_ROOT
if (-not $root) { throw 'This fixture requires an explicit isolated smoke root' }
$fullRoot = [IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
$state = [IO.Path]::GetFullPath($StateDirectory)
if (-not $state.StartsWith($fullRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Fixture state directory is outside its isolated root'
}
New-Item -ItemType Directory -Path $state -Force | Out-Null
[ordered]@{
    PID = $PID
    Environment = $env:NSSM_NEXT_SMOKE
    WorkingDirectory = [Environment]::CurrentDirectory
    StartedAt = [DateTime]::UtcNow.ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $state "started-$PID.json") -Encoding utf8NoBOM
[Console]::Error.WriteLine("NSSM_NEXT_CHILD_STDERR pid=$PID")
while ($true) {
    [IO.File]::WriteAllText((Join-Path $state 'heartbeat.txt'), [string]$PID)
    $exitFile = Join-Path $state 'exit-code.txt'
    if (Test-Path -LiteralPath $exitFile -PathType Leaf) {
        $text = [IO.File]::ReadAllText($exitFile).Trim()
        [int]$code = 0
        if (-not [int]::TryParse($text, [ref]$code) -or $code -lt 0 -or $code -gt 255) {
            throw 'Invalid fixture exit code'
        }
        Remove-Item -LiteralPath $exitFile
        exit $code
    }
    [Console]::Out.WriteLine(('NSSM_NEXT_CHILD_LOG pid=' + $PID + ' ' + ('x' * 80)))
    [Console]::Out.Flush()
    Start-Sleep -Milliseconds 100
}
