# Actual service writes are authorized only on this disposable hosted CI branch.
# Environment guards prevent accidents; they are not machine attestation.
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
if ($env:GITHUB_ACTIONS -cne 'true' -or $env:RUNNER_ENVIRONMENT -cne 'github-hosted' -or $env:RUNNER_OS -cne 'Windows' -or
    $env:GITHUB_REPOSITORY -cne 'pedoc/nssm' -or $env:GITHUB_REF -cne 'refs/heads/feat/winui3-unified-prototype' -or
    $env:GITHUB_RUN_ID -cnotmatch '^[0-9]+$') {
    throw 'Real service integration requires the explicitly authorized GitHub-hosted Windows branch.'
}
if (-not $env:RUNNER_TEMP -or -not (Test-Path -LiteralPath $env:RUNNER_TEMP -PathType Container)) {
    throw 'A disposable runner temporary directory is required.'
}
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'The hosted runner must be elevated; no local elevation is performed by this script.'
}
$nextRoot = Split-Path -Parent $PSScriptRoot
$token = [Guid]::NewGuid().ToString('N')
$sandbox = Join-Path $env:RUNNER_TEMP ('nssmnext-managed-' + $token)
$bin = Join-Path $sandbox 'runner'
$working = Join-Path $sandbox 'working with spaces'
$evidence = Join-Path $nextRoot 'out/managed-integration'
foreach ($directory in @($sandbox, $bin, $working, $evidence)) {
    if (Test-Path -LiteralPath $directory) { throw "Refusing to mix test evidence with an existing directory: $directory" }
    New-Item -ItemType Directory -Path $directory | Out-Null
}
Set-Content -LiteralPath (Join-Path $sandbox '.managed-service-test') -Value $token -NoNewline -Encoding utf8NoBOM
# Low-privilege/virtual service identities need access only to this fresh test sandbox.
& icacls.exe $sandbox /grant '*S-1-5-11:(OI)(CI)M' /T | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Cannot prepare the owned sandbox for service-identity tests.' }
$program = Join-Path $bin 'NssmNext.IntegrationTests.exe'
Copy-Item -LiteralPath (Join-Path $nextRoot 'out/Tests/NssmNext.IntegrationTests.exe') -Destination $program
Copy-Item -LiteralPath (Join-Path $nextRoot 'out/x64/Release/NssmNext.ServiceHost.exe') -Destination (Join-Path $bin 'NssmNext.ServiceHost.exe')
$application = (Get-Command pwsh.exe -CommandType Application).Source
$fixture = (Resolve-Path -LiteralPath (Join-Path $nextRoot 'tests/fixtures/managed-child.ps1')).Path
$name = 'NssmNextMgmtCI_' + $env:GITHUB_RUN_ID + '_' + $token.Substring(0, 12)
$code = -1
try {
    $output = & $program $application $fixture $sandbox $token 2>&1
    $code = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $evidence 'native-integration.log') -Encoding utf8NoBOM
    $output | Write-Output
} finally {
    $remaining = Get-CimInstance Win32_Service -Filter "Name='$name'"
    Get-ChildItem -LiteralPath $sandbox -Filter 'started-*.txt' -File | Copy-Item -Destination $evidence
    [ordered]@{
        Repository = $env:GITHUB_REPOSITORY; Ref = $env:GITHUB_REF; RunId = $env:GITHUB_RUN_ID
        Service = $name; ExitCode = $code; RemainingService = [bool]$remaining
        NativeBackend = 'real SCM on disposable runner'; LocalMachineTesting = $false
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding utf8NoBOM
}
if ($code -ne 0) { throw "Native managed-service integration failed: $code. Evidence retained; runner teardown bounds residual state." }
if ($remaining) { throw 'Owned test service is still registered after integration.' }
Write-Output 'PASS real owned-service integration and cleanup on disposable runner.'
