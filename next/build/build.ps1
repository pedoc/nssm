param(
    [ValidateSet('Core', 'App', 'Reference', 'Router', 'Capture')]
    [string]$Project = 'App',
    [string]$Proxy = '',
    [string]$WindowsTargetPlatformVersion = '',
    [ValidateSet('minimal', 'normal', 'detailed')]
    [string]$Verbosity = 'minimal',
    [switch]$RestoreOnly,
    [switch]$NoRestore,
    [switch]$UseVerifiedCache,
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$nextRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = & $vswhere -latest -products '*' -property installationPath
if (-not $vs) { throw 'Visual Studio with native C++ tools is required' }
$msbuild = Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) { throw 'Native MSBuild was not found' }

# Resolve the concrete installed Windows SDK 10 version (WindowsTargetPlatformVersion
# must be a full version such as 10.0.26100.0; the bare "10.0" value breaks mc.exe lookup).
if (-not $WindowsTargetPlatformVersion) {
    $sdkBinRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
    if (Test-Path -LiteralPath $sdkBinRoot) {
        $candidates = Get-ChildItem -LiteralPath $sdkBinRoot -Directory |
            Where-Object { $_.Name -match '^10\.0\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending
        foreach ($candidate in $candidates) {
            if (Test-Path -LiteralPath (Join-Path $candidate.FullName 'x64/mc.exe')) {
                $WindowsTargetPlatformVersion = $candidate.Name
                break
            }
        }
    }
    if (-not $WindowsTargetPlatformVersion) {
        throw 'No installed Windows SDK 10 version with x64 mc.exe was found'
    }
    Write-Host "Detected WindowsTargetPlatformVersion: $WindowsTargetPlatformVersion"
}

if ($Proxy) {
    $env:HTTPS_PROXY = $Proxy
    $env:HTTP_PROXY = $Proxy
    $env:NO_PROXY = ''
    $env:no_proxy = ''
}
$projectFile = if ($Project -in @('Reference', 'Router', 'Capture')) {
    Join-Path $nextRoot "tests/$Project/$Project.vcxproj"
} else {
    Join-Path $nextRoot "src/$Project/$Project.vcxproj"
}
$arguments = @(
    $projectFile,
    '/p:Configuration=Release',
    '/p:Platform=x64',
    "/p:WindowsTargetPlatformVersion=$WindowsTargetPlatformVersion",
    '/m',
    '/nr:false',
    '/nologo',
    "/v:$Verbosity"
)
if ($Project -eq 'App') {
    New-Item -ItemType Directory -Path (Join-Path $nextRoot 'obj/verified-source') -Force | Out-Null
    if ($RestoreOnly) { $arguments += '/t:Restore' }
    elseif (-not $NoRestore) { $arguments += '/restore' }
    $arguments += '/p:RestoreDisableParallel=true'
    $arguments += ('/p:RestoreConfigFile=' + (Join-Path $nextRoot 'NuGet.Config'))
    if ($UseVerifiedCache) {
        $arguments += ('/p:RestoreSources=' + (Join-Path $nextRoot 'obj/verified-source'))
    }
}
if ($Rebuild) {
    if ($RestoreOnly) { throw 'Rebuild and RestoreOnly cannot be combined' }
    $arguments += '/t:Rebuild'
}
& $msbuild @arguments
if ($LASTEXITCODE -ne 0) { throw "Native build failed with exit code $LASTEXITCODE" }
