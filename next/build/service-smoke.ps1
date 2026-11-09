param([string]$Executable = (Join-Path $PSScriptRoot '../out/x64/Release/NssmNext.exe'))

$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -cne 'true' -or $env:RUNNER_ENVIRONMENT -cne 'github-hosted' -or $env:RUNNER_OS -cne 'Windows') {
    throw 'Service mutations are permitted only on a GitHub-hosted Windows runner'
}
if ($env:GITHUB_REPOSITORY -cne 'pedoc/nssm' -or $env:GITHUB_REF -cne 'refs/heads/feat/winui3-unified-prototype' -or $env:GITHUB_RUN_ID -cnotmatch '^[0-9]+$') {
    throw 'Service tests require the explicitly authorized repository and experimental branch'
}
if (-not $env:RUNNER_TEMP -or -not (Test-Path -LiteralPath $env:RUNNER_TEMP -PathType Container)) {
    throw 'A runner-owned temporary directory is required'
}
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'The disposable hosted runner must provide an elevated administrator token'
}
. (Join-Path $PSScriptRoot 'ci/ServiceSmokeHelpers.ps1')
$nextRoot = Split-Path -Parent $PSScriptRoot
$sourceExe = (Resolve-Path -LiteralPath $Executable).Path
$fixture = (Resolve-Path -LiteralPath (Join-Path $nextRoot 'tests/fixtures/service-child.ps1')).Path
$sandbox = Join-Path $env:RUNNER_TEMP ('nssm-next-' + [Guid]::NewGuid().ToString('N'))
$hostDirectory = Join-Path $sandbox 'host with spaces'
$stateDirectory = Join-Path $sandbox 'state with spaces'
$workingDirectory = Join-Path $sandbox 'working directory'
$logDirectory = Join-Path $sandbox 'logs'
foreach ($directory in @($hostDirectory, $stateDirectory, $workingDirectory, $logDirectory)) {
    New-Item -ItemType Directory -Path $directory | Out-Null
}
$script:hostExe = Join-Path $hostDirectory 'NssmNext.exe'
Copy-Item -LiteralPath $sourceExe -Destination $script:hostExe
$script:serviceName = 'NssmNextCI_' + $env:GITHUB_RUN_ID + '_' + [Guid]::NewGuid().ToString('N').Substring(0, 10)
$script:evidence = [ordered]@{ Service = $serviceName; Cases = @(); Cleanup = 'pending'; Error = $null }
$evidenceDirectory = Join-Path $nextRoot 'out/service-smoke'
New-Item -ItemType Directory -Path $evidenceDirectory -Force | Out-Null

function Assert-OwnedService {
    if ($script:serviceName -cnotmatch '^NssmNextCI_[0-9]+_[a-f0-9]{10}$') { throw 'Unsafe service name' }
    $service = Get-SmokeService
    if ($service -and $service.PathName.Trim() -ine ('"' + $script:hostExe + '"')) {
        throw 'Service image path changed; refusing to control or delete it'
    }
    if ($service -and ($service.StartName -ine 'LocalSystem' -or $service.StartMode -ine 'Auto')) {
        throw 'Service account or start mode changed; refusing to control or delete it'
    }
    return $service
}

$testError = $null
$cleanupError = $null
try {
    if (Get-SmokeService) { throw 'Generated test service name already exists' }
    $target = (Get-Command pwsh.exe -CommandType Application).Source
    $null = Invoke-NssmProbe @('install', $serviceName, $target)
    $service = Assert-OwnedService
    if (-not $service -or $service.StartMode -ne 'Auto') { throw 'Install defaults changed' }
    Record-SmokePass 'install defaults and unchanged native ImagePath'

    $parameters = '-NoProfile -NonInteractive -File "' + $fixture + '" -StateDirectory "' + $stateDirectory + '"'
    $stdout = Join-Path $logDirectory 'service output.log'
    $stderr = Join-Path $logDirectory 'service error.log'
    foreach ($setting in @(
        @('AppParameters', $parameters), @('AppDirectory', $workingDirectory),
        @('AppStdout', $stdout), @('AppStderr', $stderr),
        @('AppRotateFiles', '1'), @('AppRotateOnline', '1'), @('AppRotateBytes', '1048576')
    )) {
        $null = Invoke-NssmProbe @('set', $serviceName, $setting[0], $setting[1])
    }
    $marker = 'expected-' + [Guid]::NewGuid().ToString('N')
    $null = Invoke-NssmProbe @('set', $serviceName, 'AppEnvironmentExtra', "NSSM_NEXT_SMOKE=$marker", "NSSM_NEXT_SMOKE_ROOT=$sandbox")
    if ((Invoke-NssmProbe @('get', $serviceName, 'AppDirectory')) -ine $workingDirectory) { throw 'Configuration roundtrip failed' }
    $null = Assert-OwnedService
    $null = Invoke-NssmProbe @('start', $serviceName)
    Wait-SmokeCondition { (Get-SmokeService).State -eq 'Running' -and (Get-ChildItem $stateDirectory -Filter 'started-*.json').Count -ge 1 } 'Service/target did not start'
    $started = Get-ChildItem $stateDirectory -Filter 'started-*.json' | Select-Object -First 1
    $data = Get-Content -LiteralPath $started.FullName -Raw | ConvertFrom-Json
    if ($data.Environment -cne $marker -or $data.WorkingDirectory -ine $workingDirectory) { throw 'Environment or working directory changed' }
    Record-SmokePass 'start, spaced arguments, environment and directory'

    [Diagnostics.Process]::EnterDebugMode()
    try {
        $hostProcess = Get-Process -Id (Get-SmokeService).ProcessId
        $uiModules = @($hostProcess.Modules | Where-Object { $_.ModuleName -match '^Microsoft\.UI\.|AppRuntime|dcompi' })
        if ($uiModules.Count) { throw 'Service mode loaded UI runtime modules' }
    } finally { [Diagnostics.Process]::LeaveDebugMode() }
    Record-SmokePass 'SYSTEM host runs from EXE-only directory without UI modules'

    Wait-SmokeCondition { (Test-Path $stdout) -and (Test-Path $stderr) } 'I/O redirection files missing'
    $null = Invoke-NssmProbe @('rotate', $serviceName)
    Wait-SmokeCondition { @(Get-ChildItem $logDirectory -File | Where-Object { $_.Name -notin @('service output.log', 'service error.log') }).Count -gt 0 } 'Online rotation did not produce an archive'
    Record-SmokePass 'stdout, stderr and online rotation'

    $before = @(Get-ChildItem $stateDirectory -Filter 'started-*.json').Count
    Set-Content -LiteralPath (Join-Path $stateDirectory 'exit-code.txt') -Value '17' -NoNewline
    Wait-SmokeCondition { @(Get-ChildItem $stateDirectory -Filter 'started-*.json').Count -gt $before } 'Default crash restart did not launch a new target'
    Record-SmokePass 'default unexpected-exit restart'

    $null = Invoke-NssmProbe @('set', $serviceName, 'AppExit', '17', 'Exit')
    Set-Content -LiteralPath (Join-Path $stateDirectory 'exit-code.txt') -Value '17' -NoNewline
    Wait-SmokeCondition { (Get-SmokeService).State -eq 'Stopped' } 'Configured Exit action did not stop the host'
    Record-SmokePass 'configured exit-code action'
    $null = Assert-OwnedService
    $null = Invoke-NssmProbe @('start', $serviceName)
    Wait-SmokeCondition { (Get-SmokeService).State -eq 'Running' } 'Service did not start again'
    $null = Invoke-NssmProbe @('stop', $serviceName)
    Wait-SmokeCondition { (Get-SmokeService).State -eq 'Stopped' } 'Stop did not finish'
    $null = Assert-OwnedService
    $null = Invoke-NssmProbe @('remove', $serviceName, 'confirm')
    Wait-SmokeCondition { $null -eq (Get-SmokeService) } 'Service was not removed'
    Record-SmokePass 'explicit stop and removal'
} catch {
    $testError = $_
    $evidence.Error = $_.ToString()
} finally {
    try {
        $service = Assert-OwnedService
        if ($service) {
            if ($service.State -ne 'Stopped') {
                & sc.exe stop $serviceName | Out-Null
                $stopExitCode = $LASTEXITCODE
                $ERROR_SERVICE_NOT_ACTIVE = 1062
                if ($stopExitCode -notin @(0, $ERROR_SERVICE_NOT_ACTIVE)) {
                    throw "Cleanup could not request stop for owned service (exit $stopExitCode)"
                }
                Wait-SmokeCondition { (Get-SmokeService).State -eq 'Stopped' } 'Cleanup could not stop owned service'
            }
            $null = Assert-OwnedService
            & sc.exe delete $serviceName | Out-Null
            if ($LASTEXITCODE -ne 0) { throw 'Cleanup could not delete owned service' }
            Wait-SmokeCondition { $null -eq (Get-SmokeService) } 'Cleanup left the owned service registered'
        }
        $evidence.Cleanup = 'passed'
    } catch { $cleanupError = $_; $evidence.Cleanup = $_.ToString() }
    Get-ChildItem $logDirectory -File | Copy-Item -Destination $evidenceDirectory -Force
    $evidence | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $evidenceDirectory 'result.json') -Encoding utf8NoBOM
}
if ($testError) { throw $testError }
if ($cleanupError) { throw $cleanupError }
Write-Output 'PASS isolated hosted-runner service compatibility checks'
