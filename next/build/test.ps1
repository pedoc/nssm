param([string]$Proxy = '')

$ErrorActionPreference = 'Stop'
$nextRoot = Split-Path -Parent $PSScriptRoot
$python = Join-Path $nextRoot '.venv/Scripts/python.exe'
if ($Proxy) {
    $env:HTTPS_PROXY = $Proxy
    $env:HTTP_PROXY = $Proxy
}
if (-not (Test-Path -LiteralPath $python)) {
    & python -m venv (Join-Path $nextRoot '.venv')
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create isolated test environment' }
}
& $python -m pip install --disable-pip-version-check -r (Join-Path $nextRoot 'tests/requirements.txt')
if ($LASTEXITCODE -ne 0) { throw 'Cannot install pinned test tools' }
& $python -m black --check (Join-Path $nextRoot 'tests')
if ($LASTEXITCODE -ne 0) { throw 'Python formatting check failed' }
& $python -m ruff check --no-cache (Join-Path $nextRoot 'tests')
if ($LASTEXITCODE -ne 0) { throw 'Python lint check failed' }
& $python -IB -m pytest -q -p no:cacheprovider (Join-Path $nextRoot 'tests')
if ($LASTEXITCODE -ne 0) { throw 'Compatibility contracts failed' }
