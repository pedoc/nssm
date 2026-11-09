param(
    [string]$Executable = (Join-Path $PSScriptRoot '../out/UiTests/x64/Release/NssmNext.exe'),
    [string]$CaptureExe = (Join-Path $PSScriptRoot '../out/Tests/CaptureWindow.exe')
)
$ErrorActionPreference = 'Stop'
$testRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../out/UiTests')) + [IO.Path]::DirectorySeparatorChar
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
if (-not $executablePath.StartsWith($testRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'This driver only operates the separately built UiTests application.'
}
$desktop = Get-ChildItem (Join-Path $env:ProgramFiles 'dotnet/shared/Microsoft.WindowsDesktop.App') -Directory |
    Where-Object { ([version]$_.Name).Major -eq [Environment]::Version.Major } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $desktop) { throw 'Matching desktop runtime is required for UI Automation.' }
foreach ($assembly in @('WindowsBase', 'UIAutomationTypes', 'UIAutomationClient', 'System.Drawing.Common')) {
    Add-Type -Path (Join-Path $desktop.FullName "$assembly.dll")
}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NextWindowCapture {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")] public static extern bool SetPhysicalCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out Rect rectangle);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
'@
$evidenceDirectory = Join-Path $PSScriptRoot ('../out/management-ui-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $evidenceDirectory | Out-Null
$script:results = [Collections.Generic.List[object]]::new()
$script:window = $null
$process = $null
$failure = $null
$serviceName = 'NssmNextUITest_' + [Guid]::NewGuid().ToString('N').Substring(0, 12)

function Record([string]$Name, [string]$Observation) {
    $script:results.Add([ordered]@{ Name = $Name; Observation = $Observation })
    Write-Output "OBSERVED $Name : $Observation"
}
function Wait-Ui([scriptblock]$Condition, [string]$Description) {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($process -and $process.HasExited) { throw "Application exited: $($process.ExitCode)" }
        try { if (& $Condition) { return } } catch [Windows.Automation.ElementNotAvailableException] { }
        Start-Sleep -Milliseconds 200
    }
    throw "Timed out: $Description"
}
function By-Id([string]$Id) {
    $condition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $Id)
    return $script:window.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
}
function Need([string]$Id) {
    $element = By-Id $Id
    if (-not $element) { throw "Missing control: $Id" }
    return $element
}
function Invoke-Control($Element) {
    Wait-Ui { $Element.Current.IsEnabled -and -not $Element.Current.IsOffscreen } 'button ready for input'
    if ($Element.Current.ProcessId -ne $process.Id) { throw 'Refusing input outside the owned UI process.' }
    [void][NextWindowCapture]::SetForegroundWindow($process.MainWindowHandle)
    Wait-Ui {
        [uint32]$foregroundProcess = 0
        [void][NextWindowCapture]::GetWindowThreadProcessId([NextWindowCapture]::GetForegroundWindow(), [ref]$foregroundProcess)
        $foregroundProcess -eq $process.Id
    } 'owned UI is foreground'
    $bounds = $Element.Current.BoundingRectangle
    $x = [int]($bounds.Left + $bounds.Width / 2)
    $y = [int]($bounds.Top + $bounds.Height / 2)
    $windowBounds = $script:window.Current.BoundingRectangle
    if (-not $windowBounds.Contains($x, $y)) { throw 'Refusing input outside owned window bounds.' }
    Write-Output "DRIVE click: $($Element.Current.AutomationId) | $($Element.Current.Name)"
    if (-not [NextWindowCapture]::SetPhysicalCursorPos($x, $y)) { throw 'Cannot position input inside owned UI.' }
    [NextWindowCapture]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    [NextWindowCapture]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
}
function Invoke-Id([string]$Id) { Invoke-Control (Need $Id) }
function Set-Value([string]$Id, [string]$Value) {
    (Need $Id).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($Value)
}
function Named-Button([string]$Name, [switch]$Optional) {
    $conditions = [Windows.Automation.AndCondition]::new(
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::Button),
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty, $Name))
    $button = $script:window.FindFirst([Windows.Automation.TreeScope]::Descendants, $conditions)
    if (-not $button -and -not $Optional) { throw "Missing dialog button: $Name" }
    return $button
}
function Text-In-Window([string]$Text) {
    $all = $script:window.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition)
    foreach ($element in $all) { if ($element.Current.Name -like "*$Text*") { return $true } }
    return $false
}
function Rows {
    $list = Need 'ServiceList'
    $condition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty, [Windows.Automation.ControlType]::ListItem)
    return $list.FindAll([Windows.Automation.TreeScope]::Descendants, $condition)
}
function Find-Row([string]$Name) {
    foreach ($row in (Rows)) { if ($row.Current.Name -like "*$Name*") { return $row } }
    return $null
}
function Editor-Closed {
    $editor = By-Id 'ServiceEditorDialog'
    return (-not $editor -or $editor.Current.IsOffscreen)
}
function Open-New {
    Wait-Ui { (Need 'NewButton').Current.IsEnabled } 'new button enabled'
    Invoke-Id 'NewButton'
    Wait-Ui { $null -ne (By-Id 'EditorServiceName') } 'new editor visible'
}
function Fill-New([string]$Name) {
    Set-Value 'EditorServiceName' $Name
    Set-Value 'EditorDisplayName' 'UI managed example'
    Set-Value 'EditorApplication' (Join-Path $env:WINDIR 'System32/cmd.exe')
    Set-Value 'EditorArguments' '/d /c echo memory-only'
    Set-Value 'EditorDescription' 'Created through the real UI; memory backend only.'
}
function Screenshot([string]$Name) {
    $target = Join-Path $evidenceDirectory "$Name.png"
    [void][NextWindowCapture]::ShowWindow($process.MainWindowHandle, 9)
    [void][NextWindowCapture]::SetForegroundWindow($process.MainWindowHandle)
    Start-Sleep -Milliseconds 500
    if (Test-Path -LiteralPath $CaptureExe) {
        & $CaptureExe $process.MainWindowHandle.ToInt64() $process.Id $target
        if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $target)) {
            Record 'capture' "WGC: $target"
            return
        }
    }
    $rect = [NextWindowCapture+Rect]::new()
    if (-not [NextWindowCapture]::GetWindowRect($process.MainWindowHandle, [ref]$rect)) { throw 'Cannot read owned window bounds.' }
    $image = [Drawing.Bitmap]::new(($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top))
    $graphics = [Drawing.Graphics]::FromImage($image)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $image.Size)
        $image.Save($target, [Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $image.Dispose() }
    Record 'capture' "screen region: $target"
}

try {
    if (Get-CimInstance Win32_Service -Filter "Name='$serviceName'") { throw 'Unexpected real service name collision.' }
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $executablePath; $info.WorkingDirectory = Split-Path -Parent $executablePath
    $info.UseShellExecute = $false; $info.CreateNoWindow = $false
    $process = [Diagnostics.Process]::Start($info)
    Wait-Ui { $process.Refresh(); $process.MainWindowHandle -ne [IntPtr]::Zero } 'application window'
    $script:window = [Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
    if ($process.MainWindowTitle -notlike '*内存测试*') { throw 'Refusing to drive writes outside the test backend.' }
    Wait-Ui { Text-In-Window '内存测试模式' } 'explicit memory-backend banner'
    Wait-Ui { (Need 'RefreshButton').Current.IsEnabled -and (Rows).Count -eq 2 } 'initial owned rows'
    if (Find-Row 'legacy-nssm-hidden') { throw 'Legacy/unowned service leaked into the list.' }
    Record 'list' 'Two NssmNext-owned rows visible; unowned legacy fixture excluded.'
    Set-Value 'SearchBox' 'nssmnext-worker'
    Wait-Ui { (Rows).Count -eq 1 } 'filter result'
    Set-Value 'SearchBox' ''
    Wait-Ui { (Rows).Count -eq 2 } 'filter cleared'
    Record 'filter' 'Search narrowed 2 rows to 1 and clear restored both.'

    Open-New
    Invoke-Control (Named-Button '创建服务')
    Wait-Ui { Text-In-Window '服务名必须' } 'required-name validation'
    Record 'invalid-input' 'Empty name rejected and editor stayed open.'
    Screenshot '01-validation'
    Fill-New $serviceName
    Invoke-Control (Named-Button '创建服务')
    Wait-Ui { (Editor-Closed) -and (Rows).Count -eq 3 -and $null -ne (Find-Row $serviceName) } 'created service row'
    Record 'create' "New owned row appeared: $serviceName; target was not started."
    (Find-Row $serviceName).GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Wait-Ui { (Need 'EditButton').Current.IsEnabled } 'selected service controls'
    Invoke-Id 'EditButton'
    Wait-Ui { $null -ne (By-Id 'EditorArguments') } 'edit configuration'
    if ((Need 'EditorServiceName').Current.IsEnabled) { throw 'Service name must be immutable while editing.' }
    Set-Value 'EditorDisplayName' 'UI edited service'
    Set-Value 'EditorArguments' '--edited-through-ui'
    Set-Value 'EditorDescription' 'Verified edit through WinUI controls.'
    Invoke-Control (Named-Button '保存更改')
    Wait-Ui { (Editor-Closed) -and (Need 'DetailText').Current.Name -like '*--edited-through-ui*' } 'saved details'
    Record 'edit' 'Display name/arguments/description persisted in memory; immutable service name disabled.'
    Screenshot '02-managed-service'

    Open-New; Fill-New $serviceName
    Invoke-Control (Named-Button '创建服务')
    Wait-Ui { Text-In-Window '同名服务已存在' } 'duplicate-name rejection'
    Record 'duplicate-name' 'Existing row was not overwritten; duplicate was rejected in the editor.'
    Invoke-Control (Named-Button '取消'); Wait-Ui { Editor-Closed } 'duplicate editor dismissed'
    if ((Rows).Count -ne 3) { throw 'Duplicate attempt changed row count.' }

    Invoke-Id 'StartButton'
    Wait-Ui { (Need 'DetailStatus').Current.Name -like '*运行中*' -and (Need 'StopButton').Current.IsEnabled -and (Text-In-Window '服务已启动。') } 'start state'
    Record 'start' 'Selected row changed from stopped to running.'
    Invoke-Id 'RestartButton'
    Wait-Ui { $null -ne (Named-Button '重启服务' -Optional) } 'restart confirmation'
    Invoke-Control (Named-Button '重启服务')
    Wait-Ui { (Need 'RefreshButton').Current.IsEnabled -and (Need 'DetailStatus').Current.Name -like '*运行中*' -and (Text-In-Window '服务已重启。') } 'restart completed'
    Record 'restart' 'Explicit restart confirmation completed; row returned to running.'
    Invoke-Id 'StopButton'
    Wait-Ui { (Need 'DetailStatus').Current.Name -like '*已停止*' -and (Need 'StartButton').Current.IsEnabled -and (Text-In-Window '服务已停止。') } 'stop state'
    Record 'stop' 'Selected row changed to stopped.'
    Invoke-Id 'DeleteButton'
    Wait-Ui { $null -ne (Named-Button '停止并删除' -Optional) } 'delete confirmation'
    Invoke-Control (Named-Button '取消')
    Wait-Ui { (Need 'DeleteButton').Current.IsEnabled } 'cancelled delete'
    if ((Rows).Count -ne 3) { throw 'Cancelled deletion changed the list.' }
    Record 'cancel-delete' 'Cancel retained the selected service.'
    Invoke-Id 'DeleteButton'
    Wait-Ui { $null -ne (Named-Button '停止并删除' -Optional) } 'delete confirmation again'
    Invoke-Control (Named-Button '停止并删除')
    Wait-Ui { (Rows).Count -eq 2 -and $null -eq (Find-Row $serviceName) } 'owned row removed'
    Record 'delete' 'Confirmed deletion removed only the created memory record.'
    if (Get-CimInstance Win32_Service -Filter "Name='$serviceName'") { throw 'Memory UI unexpectedly registered a real service.' }
    Record 'system-boundary' 'Read-only SCM query confirms no real test service was created.'
} catch {
    $failure = $_
    if ($process -and -not $process.HasExited) {
        try { Screenshot 'failure' } catch { Write-Warning $_.ToString() }
        try {
            $all = $script:window.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition)
            @($all | ForEach-Object { "$($_.Current.AutomationId) | $($_.Current.ControlType.ProgrammaticName) | $($_.Current.Name)" }) |
                Set-Content (Join-Path $evidenceDirectory 'failure-tree.txt') -Encoding utf8NoBOM
        } catch { Write-Warning $_.ToString() }
    }
} finally {
    if ($process) {
        if (-not $process.HasExited) {
            [void]$process.CloseMainWindow()
            if (-not $process.WaitForExit(10000)) { $process.Kill($true); if (-not $failure) { $failure = 'Owned UI process did not close normally.' } }
        }
        $process.Dispose()
    }
    [ordered]@{ Executable = $executablePath; Backend = 'memory-only'; ServiceName = $serviceName;
        Steps = @($script:results.ToArray()); Error = if ($failure) { $failure.ToString() } else { $null } } |
        ConvertTo-Json -Depth 6 | Set-Content (Join-Path $evidenceDirectory 'observations.json') -Encoding utf8NoBOM
    Write-Output "EVIDENCE=$evidenceDirectory"
}
if ($failure) { throw $failure }
Write-Output 'PASS observed management UI flow against memory backend; real service mutation not exercised.'
