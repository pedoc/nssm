param(
    [ValidatePattern('^[0-9A-Za-z][0-9A-Za-z.-]{0,80}$')]
    [string]$Version = 'snapshot',
    [ValidatePattern('^[0-9a-f]{40}$')]
    [string]$Commit,
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$nextRoot = Split-Path -Parent $PSScriptRoot
# Deliberately no arbitrary binary-input parameter: never package the UiTests backend.
$source = [IO.Path]::GetFullPath((Join-Path $nextRoot 'out/x64/Release'))
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $nextRoot "out/release-$Version" }
$output = [IO.Path]::GetFullPath($OutputDirectory)
if ($output.StartsWith($source, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package output must be outside the production runtime directory.'
}
if (Test-Path -LiteralPath $output) { throw "Refusing to overwrite existing output: $output" }
if (-not $Commit) {
    $Commit = & git -C $nextRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $Commit -cnotmatch '^[0-9a-f]{40}$') { throw 'Cannot resolve source commit.' }
}
$required = @(
    'NssmNext.exe', 'NssmNext.ServiceHost.exe', 'NssmNext.pri',
    'App.xbf', 'MainWindow.xbf', 'Microsoft.ui.xaml.dll',
    'Microsoft.UI.Xaml.Controls.dll', 'Microsoft.UI.Xaml.Controls.pri',
    'Microsoft.WindowsAppRuntime.dll'
)
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $name) -PathType Leaf)) {
        throw "Required runtime file missing: $name"
    }
}
function Assert-X64Executable([string]$Path, [bool]$Gui) {
    $reader = [IO.BinaryReader]::new([IO.File]::OpenRead($Path))
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not an executable: $Path" }
        $reader.BaseStream.Position = 0x3c
        $pe = $reader.ReadUInt32()
        $reader.BaseStream.Position = $pe
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw "Not an x64 PE executable: $Path"
        }
        if ($Gui) {
            $reader.BaseStream.Position = $pe + 24 + 68
            if ($reader.ReadUInt16() -ne 2) { throw 'GUI binary must use the Windows subsystem.' }
        }
    } finally { $reader.Dispose() }
}
Assert-X64Executable (Join-Path $source 'NssmNext.exe') $true
Assert-X64Executable (Join-Path $source 'NssmNext.ServiceHost.exe') $false
$stageParent = Join-Path $output 'staging'
$stage = Join-Path $stageParent 'NssmNext'
New-Item -ItemType Directory -Path $stage | Out-Null
$excluded = @('.pdb', '.ilk', '.lib', '.exp', '.obj', '.ipdb', '.iobj', '.log', '.tlog')
foreach ($item in Get-ChildItem -LiteralPath $source -Recurse -Force) {
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Unexpected reparse point: $($item.FullName)" }
    if ($item.PSIsContainer -or $item.Extension.ToLowerInvariant() -in $excluded) { continue }
    if ($item.Name -match '(?i)(Tests|CaptureWindow)\.exe$') { throw "Test binary in production output: $($item.Name)" }
    $relative = [IO.Path]::GetRelativePath($source, $item.FullName)
    $destination = Join-Path $stage $relative
    [IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
    Copy-Item -LiteralPath $item.FullName -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $nextRoot 'README.md'), (Join-Path $nextRoot 'THIRD_PARTY_NOTICES.md') -Destination $stage
# Include documentation/screenshots and the actual redistributed package licences.
Copy-Item -LiteralPath (Join-Path $nextRoot 'docs') -Destination (Join-Path $stage 'docs') -Recurse
$packages = Join-Path $nextRoot 'packages'
foreach ($package in Get-ChildItem -LiteralPath $packages -Directory) {
    foreach ($versionDirectory in Get-ChildItem -LiteralPath $package.FullName -Directory) {
        foreach ($notice in Get-ChildItem -LiteralPath $versionDirectory.FullName -File |
            Where-Object { $_.Name -match '^(LICENSE|NOTICE)(\..*)?$' }) {
            $noticeDirectory = Join-Path $stage "licenses/$($package.Name)/$($versionDirectory.Name)"
            [IO.Directory]::CreateDirectory($noticeDirectory) | Out-Null
            Copy-Item -LiteralPath $notice.FullName -Destination $noticeDirectory
        }
    }
}
[ordered]@{
    Version = $Version
    EngineVersion = (Get-Item -LiteralPath (Join-Path $source 'NssmNext.exe')).VersionInfo.ProductVersion
    Commit = $Commit
    Architecture = 'x64'
    Backend = 'native-services'
    WindowsAppSDK = '2.6.5'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'BUILDINFO.json') -Encoding utf8NoBOM
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | ForEach-Object {
    [ordered]@{
        Path = [IO.Path]::GetRelativePath($stage, $_.FullName).Replace('\', '/')
        Bytes = $_.Length
        SHA256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
})
$files | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $stage 'FILELIST.json') -Encoding utf8NoBOM
$archive = Join-Path $output "NssmNext-$Version-win-x64.zip"
[IO.Compression.ZipFile]::CreateFromDirectory($stageParent, $archive, [IO.Compression.CompressionLevel]::Optimal, $false)
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    foreach ($file in $files) {
        $entry = $zip.GetEntry('NssmNext/' + $file.Path)
        if (-not $entry -or $entry.Length -ne $file.Bytes) { throw "Archive is incomplete: $($file.Path)" }
        $stream = $entry.Open()
        try { $hash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant() }
        finally { $stream.Dispose() }
        if ($hash -cne $file.SHA256) { throw "Archive content mismatch: $($file.Path)" }
    }
} finally { $zip.Dispose() }
$checksum = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$checksum  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ascii
Write-Output "PACKAGE=$archive"
Write-Output "SHA256=$checksum"
Write-Output "FILES=$($files.Count)"
