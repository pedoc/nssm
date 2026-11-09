param(
    [string]$Executable = (Join-Path $PSScriptRoot '../out/UiTests/x64/Release/NssmNext.exe'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../docs/images'),
    [string]$CaptureExe = (Join-Path $PSScriptRoot '../out/Tests/CaptureWindow.exe')
)
$ErrorActionPreference = 'Stop'
$testRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../out/UiTests')) + [IO.Path]::DirectorySeparatorChar
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
if (-not $executablePath.StartsWith($testRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Documentation capture only opens the separately compiled memory backend.'
}
if (-not (Test-Path -LiteralPath $CaptureExe -PathType Leaf)) { throw 'Build the Capture project first.' }
foreach ($name in @('services.png', 'service-editor.png')) {
    if (Test-Path -LiteralPath (Join-Path $OutputDirectory $name)) { throw "Refusing to overwrite screenshot: $name" }
}
[IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($OutputDirectory)) | Out-Null
$desktop = Get-ChildItem (Join-Path $env:ProgramFiles 'dotnet/shared/Microsoft.WindowsDesktop.App') -Directory |
    Where-Object { ([version]$_.Name).Major -eq [Environment]::Version.Major } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $desktop) { throw 'Matching desktop runtime is required for UI Automation.' }
foreach ($assembly in @('WindowsBase', 'UIAutomationTypes', 'UIAutomationClient')) {
    Add-Type -Path (Join-Path $desktop.FullName "$assembly.dll")
}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NextDocsWindow {
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr window, uint command);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    private delegate bool EnumCallback(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumCallback callback, IntPtr data);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    public static IntPtr FindEditor(int process, IntPtr owner) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((window, data) => {
            uint pid; GetWindowThreadProcessId(window, out pid);
            if (pid == process && IsWindowVisible(window) && GetWindow(window, 4) == owner) {
                found = window; return false;
            }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
$process = $null
$editorHandle = [IntPtr]::Zero
function Wait-Ui([scriptblock]$Condition, [string]$Description) {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ($process.HasExited) { throw 'Memory preview exited before capture.' }
        if (& $Condition) { return }
        Start-Sleep -Milliseconds 150
    }
    throw "Timed out: $Description"
}
function By-Id($Window, [string]$Id) {
    $condition = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty, $Id)
    return $Window.FindFirst([Windows.Automation.TreeScope]::Descendants, $condition)
}
function Capture([IntPtr]$Handle, [string]$Name) {
    [void][NextDocsWindow]::SetForegroundWindow($Handle)
    Start-Sleep -Milliseconds 700
    $target = [IO.Path]::GetFullPath((Join-Path $OutputDirectory $Name))
    & $CaptureExe $Handle.ToInt64() $process.Id $target
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $target)) { throw "Window capture failed: $Name" }
    Write-Output "SCREENSHOT=$target"
}
try {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $executablePath
    $info.WorkingDirectory = Split-Path -Parent $executablePath
    $info.UseShellExecute = $false
    $process = [Diagnostics.Process]::Start($info)
    Wait-Ui { $process.Refresh(); $process.MainWindowHandle -ne [IntPtr]::Zero } 'memory main window'
    $main = [Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
    if ($process.MainWindowTitle -notlike '*内存测试*') { throw 'Refusing to interact with a production UI.' }
    Wait-Ui { $null -ne (By-Id $main 'NewButton') -and (By-Id $main 'NewButton').Current.IsEnabled } 'memory list loaded'
    $banner = By-Id $main 'ModeBar'
    if (-not $banner -or $banner.Current.ProcessId -ne $process.Id) { throw 'Missing owned memory-mode banner.' }
    Capture $process.MainWindowHandle 'services.png'
    # Open only the blank application page. Do not save, control services or open credentials.
    (By-Id $main 'NewButton').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    Wait-Ui {
        $handle = [NextDocsWindow]::FindEditor($process.Id, $process.MainWindowHandle)
        if ($handle -eq [IntPtr]::Zero) { return $false }
        $window = [Windows.Automation.AutomationElement]::FromHandle($handle)
        if ($window.Current.Name -notlike '*内存测试*' -or $null -eq (By-Id $window 'EditorApplication')) { return $false }
        $script:editorHandle = $handle
        return $true
    } 'owned memory editor'
    if ([NextDocsWindow]::GetWindow($editorHandle, 4) -ne $process.MainWindowHandle) { throw 'Editor does not belong to the captured main window.' }
    Capture $editorHandle 'service-editor.png'
} catch {
    if ($process -and -not $process.HasExited -and $main) {
        $status = By-Id $main 'StatusBar'
        if ($status) {
            foreach ($element in $status.FindAll([Windows.Automation.TreeScope]::Descendants, [Windows.Automation.Condition]::TrueCondition)) {
                if ($element.Current.Name) { Write-Host $element.Current.Name }
            }
        }
    }
    throw
} finally {
    if ($process -and -not $process.HasExited) {
        if ($editorHandle -ne [IntPtr]::Zero) {
            [void][NextDocsWindow]::PostMessage($editorHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
            Start-Sleep -Milliseconds 300
        }
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(5000)) { Stop-Process -Id $process.Id -ErrorAction Stop }
    }
}
