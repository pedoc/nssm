$ErrorActionPreference = 'Stop'
$files = @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1' -File -Recurse)
$fixtures = Join-Path $PSScriptRoot '../tests/fixtures'
if (Test-Path -LiteralPath $fixtures) { $files += @(Get-ChildItem -LiteralPath $fixtures -Filter '*.ps1' -File) }
foreach ($file in $files) {
    $tokens = $null
    $parseErrors = $null
    [Management.Automation.Language.Parser]::ParseFile(
        $file.FullName, [ref]$tokens, [ref]$parseErrors
    ) | Out-Null
    if ($parseErrors.Count) {
        throw ($parseErrors | ForEach-Object ToString | Out-String)
    }
    Write-Output "PASS PowerShell syntax: $($file.Name)"
}
