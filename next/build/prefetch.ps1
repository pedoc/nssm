param([string]$Proxy = 'http://127.0.0.1:10808')

$ErrorActionPreference = 'Stop'
$nextRoot = Split-Path -Parent $PSScriptRoot
$feed = Join-Path $nextRoot 'obj/feed'
$registeredSource = Join-Path $nextRoot 'obj/verified-source'
$packages = @(
    @{ Id = 'Microsoft.WindowsAppSDK.WinUI'; Version = '2.6.5' },
    @{ Id = 'Microsoft.WindowsAppSDK.Base'; Version = '2.6.5' },
    @{ Id = 'Microsoft.WindowsAppSDK.Foundation'; Version = '2.6.5' },
    @{ Id = 'Microsoft.WindowsAppSDK.InteractiveExperiences'; Version = '2.6.5' },
    @{ Id = 'Microsoft.Windows.CppWinRT'; Version = '3.0.260818.1' },
    @{ Id = 'Microsoft.Web.WebView2'; Version = '1.0.3719.77' },
    @{ Id = 'Microsoft.Windows.SDK.BuildTools'; Version = '10.0.26100.4654' },
    @{ Id = 'Microsoft.Windows.SDK.BuildTools.MSIX'; Version = '1.7.251221100' }
)
$env:HTTPS_PROXY = $Proxy
$env:HTTP_PROXY = $Proxy
$env:NO_PROXY = ''
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Test-CompleteArchive([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    if (Test-Path -LiteralPath ($Path + '.aria2')) { return $false }
    try {
        $zip = [IO.Compression.ZipFile]::OpenRead($Path)
        try { return $null -ne $zip.GetEntry('.signature.p7s') }
        finally { $zip.Dispose() }
    } catch [IO.InvalidDataException] { return $false }
}

foreach ($entry in $packages) {
    $package = $entry.Id
    $version = $entry.Version
    $id = $package.ToLowerInvariant()
    $directory = Join-Path $feed "$id/$version"
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    $file = Join-Path $directory "$id.$version.nupkg"
    $url = "https://api.nuget.org/v3-flatcontainer/$id/$version/$id.$version.nupkg"
    $complete = Test-CompleteArchive $file
    $aria2 = Get-Command aria2c.exe -ErrorAction SilentlyContinue
    for ($retry = 1; -not $complete -and $aria2 -and $retry -le 3; $retry++) {
        & $aria2.Source "--all-proxy=$Proxy" --continue=true --split=8 `
            --max-connection-per-server=8 --min-split-size=4M --file-allocation=none `
            --max-tries=5 --retry-wait=3 --connect-timeout=15 --timeout=60 `
            --allow-overwrite=false --auto-file-renaming=false --check-certificate=true `
            --summary-interval=30 --console-log-level=warn --download-result=hide `
            "--dir=$directory" "--out=$id.$version.nupkg" $url
        $complete = Test-CompleteArchive $file
        if (-not $complete) { Write-Output "Parallel transfer incomplete, attempt $retry" }
    }
    if (-not $complete -and (Test-Path -LiteralPath ($file + '.aria2'))) {
        throw "Incomplete segmented download retained for a safe retry: $package"
    }
    for ($attempt = 1; -not $complete -and $attempt -le 8; $attempt++) {
        Write-Output "Downloading official package $package $version, attempt $attempt"
        & curl.exe --proxy $Proxy --noproxy '' --proto '=https' --proto-redir '=https' `
            --http1.1 --fail --location --silent --show-error --continue-at - `
            --connect-timeout 15 --max-time 180 --output $file $url
        $downloadCode = $LASTEXITCODE
        $complete = Test-CompleteArchive $file
        if (-not $complete) { Write-Output "Incomplete package; curl exit $downloadCode. Retrying with resume." }
    }
    if (-not $complete) { throw "Could not download a complete signed archive: $package" }
    & nuget.exe verify -All $file -NonInteractive
    if ($LASTEXITCODE -ne 0) { throw "Signature verification failed: $package" }
    & nuget.exe add $file -Source $registeredSource -NonInteractive
    if ($LASTEXITCODE -ne 0) { throw "Cannot register verified local package: $package" }
    Write-Output "Verified official package: $package $version"
}
