function Invoke-NssmProbe([string[]]$Arguments) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $script:hostExe
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = [Text.Encoding]::UTF8
    $info.StandardErrorEncoding = [Text.Encoding]::UTF8
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        if (-not $process.Start()) { throw 'Native CLI did not start' }
        $output = $process.StandardOutput.ReadToEndAsync()
        $errorOutput = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(60000)) {
            $process.Kill($true)
            throw "Native CLI timed out: $($Arguments[0])"
        }
        $text = $output.GetAwaiter().GetResult()
        $errorText = $errorOutput.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) {
            throw "Native CLI failed ($($process.ExitCode)): $text $errorText"
        }
        return $text.Trim()
    } finally { $process.Dispose() }
}

function Get-SmokeService {
    Get-CimInstance Win32_Service -Filter "Name='$script:serviceName'"
}

function Wait-SmokeCondition([scriptblock]$Condition, [string]$Failure) {
    $deadline = [DateTime]::UtcNow.AddSeconds(45)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 250
    }
    throw $Failure
}

function Record-SmokePass([string]$Case) {
    $script:evidence.Cases += [ordered]@{ Name = $Case; Result = 'passed' }
    Write-Output "PASS service compatibility: $Case"
}
