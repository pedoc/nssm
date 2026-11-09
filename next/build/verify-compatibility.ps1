param(
    [string]$PrototypeExe = (Join-Path $PSScriptRoot '../out/x64/Release/NssmNext.exe'),
    [string]$ReferenceExe = (Join-Path $PSScriptRoot '../out/Reference/NssmReference.exe')
)

$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public static class NssmProbeErrorMode {
    [DllImport("kernel32.dll")]
    public static extern uint SetErrorMode(uint mode);
}
'@

function Invoke-VersionProbe([string]$Executable, [string]$Argument) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Executable
    $info.ArgumentList.Add($Argument)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = [Text.Encoding]::UTF8
    $info.StandardErrorEncoding = [Text.Encoding]::UTF8
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw 'CLI probe did not start' }
        $outputTask = $process.StandardOutput.ReadToEndAsync()
        $errorTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(15000)) {
            $process.Kill($true)
            throw "CLI probe timed out: $Argument"
        }
        [pscustomobject]@{
            Code = $process.ExitCode
            Output = $outputTask.GetAwaiter().GetResult()
            Error = $errorTask.GetAwaiter().GetResult()
        }
    } finally {
        $process.Dispose()
    }
}

$previousErrorMode = [NssmProbeErrorMode]::SetErrorMode(0x8003)
try {
$prototype = (Resolve-Path -LiteralPath $PrototypeExe).Path
$reference = (Resolve-Path -LiteralPath $ReferenceExe).Path
foreach ($argument in @('version', '-v', '-V', '-version', '--version', '/version')) {
    $expected = Invoke-VersionProbe $reference $argument
    $actual = Invoke-VersionProbe $prototype $argument
    if ($actual.Code -ne $expected.Code -or $actual.Output -cne $expected.Output -or $actual.Error -cne $expected.Error) {
        throw "CLI output/exit-code mismatch for $argument"
    }
    if ($actual.Code -ne 0) { throw "Version probe failed: $argument" }
    Write-Output "PASS redirected CLI parity: $argument"
}

$isolated = Join-Path ([IO.Path]::GetTempPath()) ('nssm-next-headless-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $isolated | Out-Null
$exeOnly = Join-Path $isolated 'NssmNext.exe'
Copy-Item -LiteralPath $prototype -Destination $exeOnly
$standalone = Invoke-VersionProbe $exeOnly 'version'
$expected = Invoke-VersionProbe $reference 'version'
if ($standalone.Code -ne 0 -or $standalone.Output -cne $expected.Output) {
    throw 'BLOCKER: the headless CLI cannot run without UI deployment files'
}
Write-Output 'PASS headless CLI starts with only the EXE and no UI deployment files'
Write-Output 'No service installation, modification, start, stop or removal was performed.'
} finally {
    [void][NssmProbeErrorMode]::SetErrorMode($previousErrorMode)
}
