$ErrorActionPreference = 'Stop'
$desktopRoot = Join-Path $env:ProgramFiles 'dotnet/shared/Microsoft.WindowsDesktop.App'
$desktop = Get-ChildItem -LiteralPath $desktopRoot -Directory |
    Where-Object { ([version]$_.Name).Major -eq [Environment]::Version.Major } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
foreach ($name in @('WindowsBase', 'UIAutomationTypes', 'UIAutomationClient')) {
    Add-Type -Path (Join-Path $desktop.FullName "$name.dll")
}
$expected = (Resolve-Path (Join-Path $PSScriptRoot '../out/x64/Release/NssmNext.exe')).Path
foreach ($process in @(Get-Process -Name NssmNext -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $expected })) {
    $condition = [Windows.Automation.PropertyCondition]::new(
        [Windows.Automation.AutomationElement]::ProcessIdProperty, [int]$process.Id)
    $windows = [Windows.Automation.AutomationElement]::RootElement.FindAll(
        [Windows.Automation.TreeScope]::Children, $condition)
    foreach ($window in $windows) {
        [ordered]@{ PID = $process.Id; Name = $window.Current.Name;
            Handle = $window.Current.NativeWindowHandle; Class = $window.Current.ClassName;
            Offscreen = $window.Current.IsOffscreen; Bounds = $window.Current.BoundingRectangle.ToString()
        } | ConvertTo-Json -Compress
    }
}
