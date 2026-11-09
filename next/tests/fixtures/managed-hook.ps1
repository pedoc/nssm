param([Parameter(Mandatory)][string]$RootDirectory,[Parameter(Mandatory)][string]$Token,[Parameter(Mandatory)][string]$Label)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($RootDirectory)
$marker = Join-Path $root '.managed-service-test'
if ($Token -cnotmatch '^[a-f0-9]{32}$' -or -not (Test-Path -LiteralPath $marker) -or
    (Get-Content -LiteralPath $marker -Raw).Trim() -cne $Token -or $Label -cnotmatch '^(Start-(Pre|Post)|Stop-Pre|Exit-Post|Power-(Change|Resume)|Rotate-(Pre|Post))$') {
    throw 'Hook fixture is limited to an explicitly prepared isolated sandbox.'
}
Set-Content -LiteralPath (Join-Path $root ('hook-' + $Label + '.txt')) -Value "$env:NSSM_HOOK_EVENT/$env:NSSM_HOOK_ACTION" -Encoding utf8NoBOM
Write-Output "NssmNext hook $Label probe"
