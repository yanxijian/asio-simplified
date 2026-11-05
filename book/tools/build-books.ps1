# Build all four language variants of the book with mdBook.
# mdBook 0.5 requires the config to be named book.toml in the book root, so
# this script swaps each variant config in place, builds with --dest-dir,
# and restores the original book.toml afterwards.
#
# Usage:  pwsh -File book\tools\build-books.ps1 [-MdBook <path>] [-RepoRoot <path>]
#
# The four variants (config -> dest):
#   book.toml      -> html      (zh, default)
#   book-en.toml   -> html-en
#   book-ja.toml   -> html-ja
#   book-ko.toml   -> html-ko

param(
    [string]$MdBook = 'mdbook',
    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
)

$ErrorActionPreference = 'Continue'   # native stderr (mdBook INFO) must not abort
$bookDir = Join-Path $RepoRoot 'book'
$bookToml = Join-Path $bookDir 'book.toml'
$backup = Join-Path $bookDir 'book.toml.bak'

$variants = @(
    @{ Config = 'book.toml';    Dest = 'html' },
    @{ Config = 'book-en.toml'; Dest = 'html-en' },
    @{ Config = 'book-ja.toml'; Dest = 'html-ja' },
    @{ Config = 'book-ko.toml'; Dest = 'html-ko' }
)

if (-not (Test-Path $bookToml)) {
    Write-Error "book.toml not found at $bookToml"
}
Copy-Item $bookToml $backup -Force

$failed = @()
try {
    foreach ($v in $variants) {
        $cfg = Join-Path $bookDir $v.Config
        if ($v.Config -ne 'book.toml') {
            Copy-Item $cfg $bookToml -Force
        }
        Write-Host "building $($v.Config) -> $($v.Dest)"
        # mdBook writes INFO logs to stderr; PS 5.1 wraps native stderr as
        # error records, so route them through 2>&1 -> strings and check the
        # real exit code instead.
        $out = & $MdBook build $bookDir --dest-dir (Join-Path $bookDir $v.Dest) 2>&1
        foreach ($line in $out) { Write-Host "  $line" }
        if ($LASTEXITCODE -ne 0) { $failed += $v.Config }
    }
}
finally {
    Copy-Item $backup $bookToml -Force
    Remove-Item $backup -Force
}

if ($failed.Count -gt 0) {
    Write-Error "build failed for: $($failed -join ', ')"
}
Write-Host 'all four books built'
