param([string]$Executable = (Join-Path $PSScriptRoot '../out/x64/Release/NssmNext.exe'))

$ErrorActionPreference = 'Stop'
$expected = (Resolve-Path -LiteralPath $Executable).Path
$matches = @(Get-Process -Name NssmNext -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $expected })
if ($matches.Count -gt 1) { throw 'Multiple prototype instances exist; close the intended window manually.' }
if ($matches.Count -eq 1) {
    if (-not $matches[0].CloseMainWindow()) { throw 'Cannot request a normal close of the prototype window' }
    if (-not $matches[0].WaitForExit(10000)) { throw 'Prototype did not close normally; no other process was terminated' }
    Write-Output 'Closed only the matching read-only prototype window.'
}
