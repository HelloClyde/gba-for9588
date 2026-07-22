[CmdletBinding()]
param(
    [string]$EmulatorRoot = $(
        if ($env:BBK9588_EMULATOR_ROOT) {
            $env:BBK9588_EMULATOR_ROOT
        } else {
            'E:\bbk9588-emulator-v0.1.5'
        }
    ),
    [int]$Port = 8013,
    [ValidateRange(1, 10000)]
    [int]$Frames = 120,
    [ValidateRange(1, 300)]
    [int]$RunSeconds = 10,
    [ValidateRange(1, 120)]
    [int]$BootDelaySeconds = 12,
    [ValidateSet('Dynarec', 'Interpreter', 'PatchedInterpreter')]
    [string]$Core = 'Dynarec',
    [Alias('Fixture')]
    [string[]]$FixtureName,
    [switch]$ResetImage,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$emulatorRootPath = (Resolve-Path -LiteralPath $EmulatorRoot).Path
$manifestPath = Join-Path $repoRoot 'tests\roms\public-fixtures.psd1'
$dependencyRoot = Join-Path $repoRoot '.deps\gba-tests'
$buildScript = Join-Path $PSScriptRoot 'build.ps1'
$verifyScript = Join-Path $PSScriptRoot 'verify_public_test_roms.ps1'
$deployScript = Join-Path $repoRoot 'sdk\scripts\test_bda_in_emulator.ps1'
$target = if ($Core -eq 'Interpreter') {
    'gpsp_headless'
} elseif ($Core -eq 'PatchedInterpreter') {
    'gpsp_headless_patched'
} else {
    'gpsp_dynarec'
}
$expectedCoreMode = if ($Core -eq 'Dynarec') { 'DYNAREC' } else { 'INTERPRETER' }
$outputRoot = Join-Path $repoRoot "tests\output\public-roms\$($Core.ToLowerInvariant())"
$bdaPath = Join-Path $repoRoot "build\$target\$target.bda"
$baseUrl = "http://127.0.0.1:$Port"
$gameDirectory = '/GAMEBOY'
$logPath = "$gameDirectory/GPSPHL.LOG"
$rawFramePath = "$gameDirectory/GPSPHL.RAW"

function Invoke-FrontendApi {
    param(
        [ValidateSet('Get', 'Post')]
        [string]$Method,
        [string]$Path,
        [object]$Body,
        [int]$TimeoutSeconds = 60
    )

    $arguments = @{
        Method = $Method
        Uri = "$baseUrl$Path"
        TimeoutSec = $TimeoutSeconds
    }
    if ($null -ne $Body) {
        $arguments.ContentType = 'application/json; charset=utf-8'
        $arguments.Body = $Body | ConvertTo-Json -Compress
    }
    Invoke-RestMethod @arguments
}

function Get-FrontendStatus {
    try {
        Invoke-FrontendApi -Method Get -Path '/api/status' -TimeoutSeconds 5
    } catch {
        $null
    }
}

function Wait-FrontendRunning {
    $deadline = (Get-Date).AddSeconds(60)
    do {
        $status = Get-FrontendStatus
        if ($status -and $status.running) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    throw "Port $Port emulator did not start within 60 seconds"
}

function Stop-Guest {
    $status = Get-FrontendStatus
    if (-not $status -or -not $status.running) {
        return
    }

    try {
        Invoke-FrontendApi -Method Post -Path '/api/stop' -TimeoutSeconds 50 | Out-Null
    } catch {
        Write-Warning "Safe shutdown failed; forcing the dedicated test guest to stop: $($_.Exception.Message)"
        Invoke-FrontendApi -Method Post -Path '/api/command' `
            -Body @{ op = 'force-stop' } -TimeoutSeconds 30 | Out-Null
    }

    $deadline = (Get-Date).AddSeconds(20)
    do {
        $status = Get-FrontendStatus
        if (-not $status -or -not $status.running) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ((Get-Date) -lt $deadline)
    throw "Port $Port emulator is still running after stop"
}

function Send-Touch {
    param([int]$X, [int]$Y)

    Invoke-FrontendApi -Method Post `
        -Path "/api/touch?x=$X&y=$Y&down=1" -TimeoutSeconds 15 | Out-Null
    Start-Sleep -Milliseconds 150
    Invoke-FrontendApi -Method Post `
        -Path "/api/touch?x=$X&y=$Y&down=0" -TimeoutSeconds 15 | Out-Null
}

function Ensure-GameDirectory {
    $parent = '/'
    $encodedParent = [Uri]::EscapeDataString($parent)
    $listing = Invoke-FrontendApi -Method Get `
        -Path "/api/files?path=$encodedParent" -TimeoutSeconds 60
    $exists = $listing.entries | Where-Object {
        $_.is_dir -and $_.name -ieq 'GAMEBOY'
    }
    if ($exists) {
        return
    }

    Stop-Guest
    Invoke-FrontendApi -Method Post -Path '/api/files/mkdir' `
        -Body @{ path = $parent; name = 'GAMEBOY' } -TimeoutSeconds 120 | Out-Null
    Wait-FrontendRunning
}

function Import-Rom {
    param([string]$RomPath)

    Stop-Guest
    $encodedDirectory = [Uri]::EscapeDataString($gameDirectory)
    $uri = "$baseUrl/api/files/import?path=$encodedDirectory&name=TEST.GBA"
    try {
        Invoke-RestMethod -Method Post -Uri $uri -InFile $RomPath `
            -ContentType 'application/octet-stream' -TimeoutSec 180 | Out-Null
    } catch {
        $importError = $_
        $importDetails = [string]$importError.ErrorDetails.Message
        if ($importDetails -match 'cluster chain|FREE_CLUSTER') {
            throw 'The dedicated emulator NAND has a damaged FAT cluster chain; rerun with -ResetImage'
        }
        $verificationCopy = [IO.Path]::GetTempFileName()
        try {
            Export-NandFile -NandPath "$gameDirectory/TEST.GBA" `
                -OutputPath $verificationCopy
            $sourceHash = (Get-FileHash -LiteralPath $RomPath -Algorithm SHA256).Hash
            $nandHash = (Get-FileHash -LiteralPath $verificationCopy -Algorithm SHA256).Hash
            if ($sourceHash -ne $nandHash) {
                throw $importError
            }
            Write-Warning 'ROM import committed before reset failed; verified NAND SHA-256 and retrying reset'
            Invoke-FrontendApi -Method Post -Path '/api/reset' -TimeoutSeconds 60 | Out-Null
        } finally {
            Remove-Item -LiteralPath $verificationCopy -Force -ErrorAction SilentlyContinue
        }
    }
    Wait-FrontendRunning
}

function Export-NandFile {
    param([string]$NandPath, [string]$OutputPath)

    $encodedPath = [Uri]::EscapeDataString($NandPath)
    Invoke-WebRequest -Uri "$baseUrl/api/files/export?path=$encodedPath" `
        -OutFile $OutputPath -TimeoutSec 120 | Out-Null
}

function Get-LastLogHex {
    param([string]$Text, [string]$Label)

    $matches = [regex]::Matches(
        $Text,
        "(?m)^$([regex]::Escape($Label))0x([0-9A-Fa-f]{8})\r?$"
    )
    if ($matches.Count -eq 0) {
        throw "Headless log is missing $Label"
    }
    [Convert]::ToUInt32($matches[$matches.Count - 1].Groups[1].Value, 16)
}

function Get-BaselineHex {
    param([object]$Baseline, [string]$Name, [string]$FixtureName)

    if ($null -eq $Baseline -or -not $Baseline.ContainsKey($Name)) {
        throw "Fixture $FixtureName is missing Headless120.$Name"
    }
    $text = ([string]$Baseline[$Name]).Trim()
    if ($text -notmatch '^(?:0x)?([0-9A-Fa-f]{8})$') {
        throw "Fixture $FixtureName has invalid Headless120.${Name}: $text"
    }
    [Convert]::ToUInt32($Matches[1], 16)
}

if (-not $SkipBuild) {
    & $buildScript -Target $target -HeadlessFrames $Frames
    if ($LASTEXITCODE -ne 0) {
        throw "$target build failed"
    }
}
if (-not (Test-Path -LiteralPath $bdaPath -PathType Leaf)) {
    throw "Headless BDA not found: $bdaPath"
}

$verifiedFixtures = @(& $verifyScript)
if ($LASTEXITCODE -ne 0) {
    throw 'Public ROM fixture verification failed'
}
Write-Host "Verified $($verifiedFixtures.Count) public ROM fixtures"

$fixtures = Import-PowerShellDataFile -LiteralPath $manifestPath
$selectedNames = if ($FixtureName.Count -gt 0) {
    @($FixtureName)
} else {
    @($fixtures.Keys | Sort-Object)
}
foreach ($name in $selectedNames) {
    if (-not $fixtures.ContainsKey($name)) {
        throw "Unknown public ROM fixture: $name"
    }
}

$deployArguments = @{
    Bda = $bdaPath
    EmulatorRoot = $emulatorRootPath
    Port = $Port
    NoAutoLaunch = $true
    NoOpenBrowser = $true
    ResetImage = [bool]$ResetImage
}
& $deployScript @deployArguments
if ($LASTEXITCODE -ne 0) {
    throw 'Headless BDA deployment failed'
}
Wait-FrontendRunning
Ensure-GameDirectory
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

$results = @()
try {
    foreach ($name in $selectedNames) {
        $fixtureEntry = $fixtures[$name]
        $relativePath = ([string]$fixtureEntry.Path).Replace(
            '/', [IO.Path]::DirectorySeparatorChar
        )
        $romPath = [IO.Path]::GetFullPath((Join-Path $dependencyRoot $relativePath))
        $logOutput = Join-Path $outputRoot "$name.log"
        $rawFrameOutput = Join-Path $outputRoot "$name.rgb565"
        Write-Host "[$name] importing $relativePath"
        $stage = 'ROM import'

        try {
            Import-Rom -RomPath $romPath
            $stage = 'firmware boot'
            Start-Sleep -Seconds $BootDelaySeconds
            $stage = 'launcher navigation'
            Send-Touch -X 198 -Y 84
            Start-Sleep -Seconds 2
            Send-Touch -X 120 -Y 100
            $stage = 'headless execution'
            Start-Sleep -Seconds $RunSeconds
            $stage = 'guest shutdown'
            Stop-Guest
            $stage = 'log export'
            Export-NandFile -NandPath $logPath -OutputPath $logOutput

            $stage = 'log validation'
            $log = Get-Content -LiteralPath $logOutput -Raw -Encoding ascii
            if ($log -notmatch "(?m)^CORE_MODE=$expectedCoreMode\r?$" -or
                $log -notmatch '(?m)^RESULT=PASS\r?$' -or
                $log -notmatch '(?m)^VIDEO_RAW_WRITE=PASS\r?$') {
                throw "Headless result did not pass in $expectedCoreMode mode"
            }
            $stage = 'raw frame export'
            Export-NandFile -NandPath $rawFramePath -OutputPath $rawFrameOutput
            if ((Get-Item -LiteralPath $rawFrameOutput).Length -ne 240 * 160 * 2) {
                throw "Raw frame size is not 76800 bytes: $rawFrameOutput"
            }
            $stage = 'counter and signature validation'
            $loaded = Get-LastLogHex -Text $log -Label 'LOAD_GAME='
            $frameIndex = Get-LastLogHex -Text $log -Label 'FRAME_INDEX='
            $videoFrames = Get-LastLogHex -Text $log -Label 'VIDEO_FRAMES='
            $audioFrames = Get-LastLogHex -Text $log -Label 'AUDIO_FRAMES='
            $videoHash = Get-LastLogHex -Text $log -Label 'VIDEO_HASH='
            $videoLastHash = Get-LastLogHex -Text $log -Label 'VIDEO_LAST_HASH='
            $gbaPc = Get-LastLogHex -Text $log -Label 'GBA_PC='
            $gbaCpsr = Get-LastLogHex -Text $log -Label 'GBA_CPSR='
            if ($loaded -ne 1u -or $frameIndex -ne [uint32]$Frames -or
                $videoFrames -ne [uint32]$Frames -or $audioFrames -eq 0u) {
                throw "Unexpected counters: loaded=$loaded frame=$frameIndex video=$videoFrames audio=$audioFrames"
            }
            if ($Frames -eq 120 -and $Core -eq 'Dynarec') {
                $baseline = $fixtureEntry.Headless120
                $checks = @{
                    VideoHash = $videoHash
                    VideoLastHash = $videoLastHash
                    GbaPc = $gbaPc
                    GbaCpsr = $gbaCpsr
                }
                foreach ($check in $checks.GetEnumerator()) {
                    $expected = Get-BaselineHex -Baseline $baseline `
                        -Name $check.Key -FixtureName $name
                    if ([uint32]$check.Value -ne $expected) {
                        throw "Headless120.$($check.Key) mismatch: expected 0x$($expected.ToString('X8')), got 0x$(([uint32]$check.Value).ToString('X8'))"
                    }
                }
            }
            $results += [pscustomobject]@{
                Name = $name
                Core = $Core
                Category = [string]$fixtureEntry.Category
                Result = 'PASS'
                Frames = $videoFrames
                AudioFrames = $audioFrames
                VideoHash = $videoHash.ToString('X8')
                VideoLastHash = $videoLastHash.ToString('X8')
                GbaPc = $gbaPc.ToString('X8')
                GbaCpsr = $gbaCpsr.ToString('X8')
                Log = $logOutput
                RawFrame = $rawFrameOutput
            }
            Write-Host "[$name] PASS ($videoFrames frames)"
        } catch {
            $results += [pscustomobject]@{
                Name = $name
                Core = $Core
                Category = [string]$fixtureEntry.Category
                Result = 'FAIL'
                Frames = 0
                AudioFrames = 0
                VideoHash = $null
                VideoLastHash = $null
                GbaPc = $null
                GbaCpsr = $null
                Log = $logOutput
                RawFrame = $rawFrameOutput
            }
            Write-Warning "[$name] FAIL during ${stage}: $($_.Exception.Message)"
            Stop-Guest
        }
    }
} finally {
    Stop-Guest
}

$summaryPath = Join-Path $outputRoot 'summary.json'
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $summaryPath -Encoding utf8
$results | Format-Table Name, Core, Category, Result, Frames, AudioFrames -AutoSize
Write-Host "Summary: $summaryPath"

$failed = @($results | Where-Object Result -ne 'PASS')
if ($failed.Count -gt 0) {
    throw "$($failed.Count) public ROM regression(s) failed"
}
