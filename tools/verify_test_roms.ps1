[CmdletBinding()]
param(
    [string]$FixtureFile = (Join-Path $PSScriptRoot '..\tests\roms\fixtures.local.psd1')
)

$ErrorActionPreference = 'Stop'
$fixturePath = [System.IO.Path]::GetFullPath($FixtureFile)

if (-not (Test-Path -LiteralPath $fixturePath -PathType Leaf)) {
    throw "Fixture file not found: $fixturePath"
}

$fixtures = Import-PowerShellDataFile -LiteralPath $fixturePath
if ($fixtures.Count -eq 0) {
    throw "No ROM fixtures are defined in: $fixturePath"
}

foreach ($entry in $fixtures.GetEnumerator()) {
    $name = $entry.Key
    $fixture = $entry.Value
    $romPath = [string]$fixture.Path

    if (-not (Test-Path -LiteralPath $romPath -PathType Leaf)) {
        throw "ROM fixture '$name' was not found: $romPath"
    }

    $file = Get-Item -LiteralPath $romPath
    if ($file.Length -ne [int64]$fixture.Size) {
        throw "ROM fixture '$name' size mismatch: expected $($fixture.Size), got $($file.Length)"
    }

    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $romPath).Hash.ToLowerInvariant()
    $expectedHash = ([string]$fixture.SHA256).ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "ROM fixture '$name' SHA-256 mismatch: expected $expectedHash, got $actualHash"
    }

    $header = [byte[]]::new(0xC0)
    $stream = [System.IO.File]::OpenRead($romPath)
    try {
        $read = $stream.Read($header, 0, $header.Length)
    }
    finally {
        $stream.Dispose()
    }
    if ($read -ne $header.Length) {
        throw "ROM fixture '$name' is too short for a complete GBA header"
    }

    $title = [System.Text.Encoding]::ASCII.GetString($header, 0xA0, 12).Trim([char]0)
    $gameCode = [System.Text.Encoding]::ASCII.GetString($header, 0xAC, 4)
    $makerCode = [System.Text.Encoding]::ASCII.GetString($header, 0xB0, 2)

    if ($title -ne [string]$fixture.HeaderTitle) {
        throw "ROM fixture '$name' title mismatch: expected '$($fixture.HeaderTitle)', got '$title'"
    }
    if ($gameCode -ne [string]$fixture.GameCode) {
        throw "ROM fixture '$name' game code mismatch: expected '$($fixture.GameCode)', got '$gameCode'"
    }
    if ($makerCode -ne [string]$fixture.MakerCode) {
        throw "ROM fixture '$name' maker code mismatch: expected '$($fixture.MakerCode)', got '$makerCode'"
    }
    if ($header[0xB2] -ne 0x96) {
        throw "ROM fixture '$name' has invalid GBA fixed byte: 0x$($header[0xB2].ToString('x2'))"
    }
    if ($header[0xBC] -ne [byte]$fixture.Version) {
        throw "ROM fixture '$name' version mismatch: expected $($fixture.Version), got $($header[0xBC])"
    }

    $headerSum = 0
    for ($index = 0xA0; $index -le 0xBC; $index++) {
        $headerSum = ($headerSum + $header[$index]) -band 0xFF
    }
    $expectedHeaderChecksum = (0 - $headerSum - 0x19) -band 0xFF
    if ($header[0xBD] -ne $expectedHeaderChecksum) {
        throw "ROM fixture '$name' header checksum mismatch: expected 0x$($expectedHeaderChecksum.ToString('x2')), got 0x$($header[0xBD].ToString('x2'))"
    }

    [pscustomobject]@{
        Name           = $name
        Path           = $file.FullName
        SizeMiB        = [math]::Round($file.Length / 1MB, 2)
        SHA256         = $actualHash
        HeaderTitle    = $title
        GameCode       = $gameCode
        MakerCode      = $makerCode
        Version        = $header[0xBC]
        HeaderChecksum = 'valid'
    }
}
