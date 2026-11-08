$ErrorActionPreference = 'Stop'
$processes = @(Get-CimInstance Win32_Process -Filter "Name='MSBuild.exe' OR Name='pwsh.exe' OR Name='NuGet.exe'")
$matching = @($processes | Where-Object { $_.CommandLine -match 'OpenSource[\\/]+nssm[\\/]+next' })
$matching | Select-Object ProcessId, ParentProcessId, CreationDate, CommandLine | ConvertTo-Json -Depth 3
$parents = @($matching.ProcessId)
$children = @($processes | Where-Object { $_.ParentProcessId -in $parents })
$children | Select-Object ProcessId, ParentProcessId, CreationDate, CommandLine | ConvertTo-Json -Depth 3
