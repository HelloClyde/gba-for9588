[CmdletBinding()]
param(
    [string]$Manifest = (Join-Path $PSScriptRoot '..\tests\roms\public-fixtures.psd1')
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lock = Import-PowerShellDataFile -LiteralPath (Join-Path $repoRoot 'deps.lock.psd1')
$dependencyRoot = Join-Path $repoRoot '.deps\gba-tests'
$manifestPath = [IO.Path]::GetFullPath($Manifest)

if (-not (Test-Path -LiteralPath (Join-Path $dependencyRoot '.git'))) {
    throw "gba-tests is not bootstrapped: $dependencyRoot"
}

$actualCommit = (& git -C $dependencyRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne [string]$lock.GbaTests.Commit) {
    throw "gba-tests revision mismatch: expected $($lock.GbaTests.Commit), got $actualCommit"
}

$fixtures = Import-PowerShellDataFile -LiteralPath $manifestPath
if ($fixtures.Count -eq 0) {
    throw "No public ROM fixtures are defined in: $manifestPath"
}

foreach ($entry in $fixtures.GetEnumerator() | Sort-Object Key) {
    $name = $entry.Key
    $fixture = $entry.Value
    $relativePath = ([string]$fixture.Path).Replace('/', [IO.Path]::DirectorySeparatorChar)
    $romPath = [IO.Path]::GetFullPath((Join-Path $dependencyRoot $relativePath))
    $dependencyPrefix = [IO.Path]::GetFullPath($dependencyRoot).TrimEnd('\') + '\'

    if (-not $romPath.StartsWith($dependencyPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Public fixture '$name' escapes gba-tests: $romPath"
    }
    if (-not (Test-Path -LiteralPath $romPath -PathType Leaf)) {
        throw "Public fixture '$name' was not found: $romPath"
    }

    $file = Get-Item -LiteralPath $romPath
    if ($file.Length -ne [int64]$fixture.Size) {
        throw "Public fixture '$name' size mismatch: expected $($fixture.Size), got $($file.Length)"
    }

    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $romPath).Hash.ToLowerInvariant()
    $expectedHash = ([string]$fixture.SHA256).ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "Public fixture '$name' SHA-256 mismatch: expected $expectedHash, got $actualHash"
    }

    if ([string]::IsNullOrWhiteSpace([string]$fixture.ExpectedScreen)) {
        throw "Public fixture '$name' has no ExpectedScreen description"
    }

    $baseline = $fixture.Headless120
    $baselineFields = @('VideoHash', 'VideoLastHash', 'GbaPc', 'GbaCpsr')
    if (-not ($baseline -is [Collections.IDictionary])) {
        throw "Public fixture '$name' has no Headless120 baseline"
    }
    if ($baseline.Count -ne $baselineFields.Count) {
        throw "Public fixture '$name' Headless120 baseline must contain exactly: $($baselineFields -join ', ')"
    }
    foreach ($field in $baselineFields) {
        $value = [string]$baseline[$field]
        if (-not $baseline.Contains($field) -or $value -notmatch '^[0-9A-Fa-f]{8}$') {
            throw "Public fixture '$name' has invalid Headless120.$field value: '$value'"
        }
    }

    $header = [byte[]]::new(0xC0)
    $stream = [IO.File]::OpenRead($romPath)
    try {
        $read = $stream.Read($header, 0, $header.Length)
    } finally {
        $stream.Dispose()
    }
    if ($read -ne $header.Length -or $header[0xB2] -ne 0x96) {
        throw "Public fixture '$name' has an invalid GBA header"
    }

    [pscustomobject]@{
        Name = $name
        Category = [string]$fixture.Category
        ExpectedScreen = [string]$fixture.ExpectedScreen
        Size = $file.Length
        SHA256 = $actualHash
        Path = $file.FullName
    }
}
