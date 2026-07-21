[CmdletBinding()]
param(
    [switch]$SkipToolchain
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$lock = Import-PowerShellDataFile -LiteralPath (Join-Path $repoRoot 'deps.lock.psd1')
$depsRoot = Join-Path $repoRoot '.deps'
$sdkRoot = Join-Path $repoRoot 'sdk'

function Invoke-Git([string[]]$Arguments) {
    & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git failed: git $($Arguments -join ' ')"
    }
}

function Sync-Dependency([string]$Name, [hashtable]$Spec) {
    $destination = Join-Path $depsRoot $Name
    $newCheckout = $false
    if (-not (Test-Path -LiteralPath (Join-Path $destination '.git'))) {
        New-Item -ItemType Directory -Force -Path $depsRoot | Out-Null
        Invoke-Git @('clone', '--no-checkout', $Spec.Url, $destination)
        $newCheckout = $true
    }

    if (-not $newCheckout) {
        $dirty = & git -C $destination status --porcelain
        if ($LASTEXITCODE -ne 0) {
            throw "Unable to inspect dependency checkout: $destination"
        }
        if ($dirty) {
            throw "Dependency checkout has local changes: $destination"
        }
    }

    & git -C $destination cat-file -e "$($Spec.Commit)^{commit}" 2>$null
    if ($LASTEXITCODE -ne 0) {
        Invoke-Git @('-C', $destination, 'fetch', '--depth', '1', 'origin', $Spec.Commit)
    }
    Invoke-Git @('-C', $destination, 'checkout', '--detach', $Spec.Commit)

    $actual = (& git -C $destination rev-parse HEAD).Trim()
    if ($actual -ne $Spec.Commit) {
        throw "Dependency revision mismatch for ${Name}: expected $($Spec.Commit), got $actual"
    }
    Write-Host "$Name ready at $actual"
}

if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot 'scripts\setup_toolchain.ps1') -PathType Leaf)) {
    Invoke-Git @('submodule', 'update', '--init', 'sdk')
}
if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot 'scripts\setup_toolchain.ps1') -PathType Leaf)) {
    throw "SDK submodule is unavailable: $sdkRoot"
}

Sync-Dependency 'gpsp' $lock.Gpsp
Sync-Dependency 'gba-tests' $lock.GbaTests

if (-not $SkipToolchain) {
    $setup = Join-Path $sdkRoot 'scripts\setup_toolchain.ps1'
    & $setup
    if ($LASTEXITCODE -ne 0) {
        throw 'SDK toolchain setup failed'
    }
}
