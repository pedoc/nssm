param(
    [string]$Executable = (Join-Path $PSScriptRoot '../out/x64/Release/NssmNext.exe'),
    [switch]$CaptureScreenshot,
    [switch]$ExplicitUi,
    [string]$Screenshot = (Join-Path $PSScriptRoot '../out/ui-smoke.png'),
    [string]$CaptureExe = (Join-Path $PSScriptRoot '../out/Tests/CaptureWindow.exe')
)

$ErrorActionPreference = 'Stop'
$desktopRoot = Join-Path $env:ProgramFiles 'dotnet/shared/Microsoft.WindowsDesktop.App'
$desktop = Get-ChildItem -LiteralPath $desktopRoot -Directory |
    Where-Object { ([version]$_.Name).Major -eq [Environment]::Version.Major } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $desktop) { throw 'Matching desktop runtime is needed for UI Automation' }
foreach ($assembly in @('WindowsBase', 'UIAutomationTypes', 'UIAutomationClient')) {
    Add-Type -Path (Join-Path $desktop.FullName "$assembly.dll")
}

function Wait-ForProbe([scriptblock]$Condition, [string]$Failure) {
    $deadline = [DateTime]::UtcNow.AddSeconds(25)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 250
    }
    throw $Failure
}

function Find-Control($Window, [string]$Identifier) {
    $condition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::AutomationIdProperty, $Identifier)
    $element = $Window.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
    if (-not $element) { throw "Automation control missing: $Identifier" }
    return $element
}

function Get-ServiceItems($List) {
    $condition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::ControlTypeProperty,
        [Windows.Automation.ControlType]::ListItem)
    return $List.FindAll([Windows.Automation.TreeScope]::Descendants, $condition)
}

function Test-NonBlankClient($Image) {
    if ($Image.Width -lt 100 -or $Image.Height -lt 150) { return $false }
    $first = $Image.GetPixel(40, 100).ToArgb()
    for ($y = 100; $y -lt $Image.Height - 40; $y += 24) {
        for ($x = 40; $x -lt $Image.Width - 40; $x += 24) {
            if ($Image.GetPixel($x, $y).ToArgb() -ne $first) { return $true }
        }
    }
    return $false
}

$launchMode = if ($ExplicitUi) { 'explicit-ui' } else { 'default-gui' }
$result = [ordered]@{ Window = 'pending'; Refresh = 'pending'; Search = 'pending';
    FilterEffect = 'pending'; Selection = 'not-run-no-data'; Close = 'pending';
    Capture = 'not-requested'; ApplicationVisualState = 'not-automatically-verified'; LaunchMode = $launchMode }
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = (Resolve-Path -LiteralPath $Executable).Path
$info.WorkingDirectory = Split-Path -Parent $info.FileName
if ($ExplicitUi) { $info.ArgumentList.Add('--ui') }
$info.UseShellExecute = $false
$info.CreateNoWindow = $false
$process = [Diagnostics.Process]::Start($info)
$testError = $null
try {
    Wait-ForProbe {
        $process.Refresh()
        if ($process.HasExited) { throw "UI exited: $($process.ExitCode)" }
        $process.MainWindowHandle -ne [IntPtr]::Zero
    } 'No application window appeared'
    if ($process.MainWindowTitle -notlike '*NSSM Next*') { throw "Unexpected window: $($process.MainWindowTitle)" }
    $window = [Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
    $result.Window = 'passed'
    $refresh = Find-Control $window 'RefreshButton'
    Wait-ForProbe { $refresh.Current.IsEnabled } 'Initial query did not finish'
    $refresh.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    Wait-ForProbe {
        if (-not $refresh.Current.IsEnabled) { return $false }
        $elements = $window.FindAll([Windows.Automation.TreeScope]::Descendants,
            [Windows.Automation.Condition]::TrueCondition)
        foreach ($element in $elements) { if ($element.Current.Name -like '*已查询*') { return $true } }
        return $false
    } 'Refresh did not complete a read-only query'
    $result.Refresh = 'passed'
    $list = Find-Control $window 'ServiceList'
    $initial = Get-ServiceItems $list
    $initialCount = $initial.Count
    $search = Find-Control $window 'SearchBox'
    $value = $search.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)
    $query = 'nssm-next-probe-' + [Guid]::NewGuid().ToString('N')
    $value.SetValue($query)
    Wait-ForProbe { (Get-ServiceItems $list).Count -eq 0 } 'Search did not filter the list'
    $result.Search = 'passed'
    $result.FilterEffect = if ($initialCount) { 'passed' } else { 'not-run-no-data' }
    $value.SetValue('')
    Wait-ForProbe { (Get-ServiceItems $list).Count -eq $initialCount } 'Search clear did not restore the list'
    if ($initialCount) {
        $items = Get-ServiceItems $list
        $items[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
        $title = Find-Control $window 'DetailTitle'
        Wait-ForProbe { $title.Current.Name -and $title.Current.Name -ne '选择一个服务' } 'Selection did not update details'
        $result.Selection = 'passed'
    }
    if ($CaptureScreenshot) {
        Add-Type -Path (Join-Path $desktop.FullName 'System.Drawing.Common.dll')
        $capture = (Resolve-Path -LiteralPath $CaptureExe).Path
        $target = [IO.Path]::GetFullPath($Screenshot)
        New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
        & $capture $process.MainWindowHandle.ToInt64() $process.Id $target
        if ($LASTEXITCODE -ne 0) { throw "Scoped capture failed: $LASTEXITCODE" }
        $image = [Drawing.Bitmap]::FromFile($target)
        try { $visible = Test-NonBlankClient $image }
        finally { $image.Dispose() }
        $result.Capture = if ($visible) { 'nonblank-capture' } else { 'inconclusive-blank-capture' }
        Write-Output "SCREENSHOT=$target"
        if (-not $visible) { throw 'Capture is blank; this cannot establish the application visual state. Manual observation is separate evidence.' }
    }
} catch {
    $testError = $_
} finally {
    if (-not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(10000)) {
            $process.Kill($true)
            $result.Close = 'failed-owned-process-terminated'
        } else { $result.Close = 'passed' }
    } else { $result.Close = 'already-exited' }
    $process.Dispose()
    $evidence = Join-Path $PSScriptRoot '../out/ui-smoke-result.json'
    New-Item -ItemType Directory -Path (Split-Path -Parent $evidence) -Force | Out-Null
    $result | ConvertTo-Json | Set-Content -LiteralPath $evidence -Encoding utf8NoBOM
    $result | ConvertTo-Json
}
if ($testError) { throw $testError }
if ($result.Close -ne 'passed') { throw 'The prototype did not close normally' }
