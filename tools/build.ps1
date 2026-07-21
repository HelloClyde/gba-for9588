[CmdletBinding()]
param(
    [ValidateSet('m0_smoke', 'm1_runtime', 'm1_av', 'm6_save', 'gpsp_headless', 'gpsp_dynarec', 'gpsp_app_interpreter', 'gpsp_app', 'gpsp_app_cpu_test')]
    [string]$Target = 'm0_smoke',
    [ValidateRange(0, 10000)]
    [int]$HeadlessFrames = 0,
    [switch]$SkipBootstrap
)

$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sdkRoot = Join-Path $repoRoot 'sdk'
$gpspRoot = Join-Path $repoRoot '.deps\gpsp'
$formalGbaTitle = 'GBA'

if (-not $SkipBootstrap) {
    & (Join-Path $PSScriptRoot 'bootstrap.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw 'Dependency bootstrap failed'
    }
}

function Find-Tool([string]$Name) {
    $toolchain = Join-Path $sdkRoot '.toolchain'
    $candidates = @(
        (Join-Path $toolchain "bin\mipsel-none-elf-$Name.exe"),
        (Join-Path $toolchain "g++-mipsel-none-elf-15.2.0\bin\mipsel-none-elf-$Name.exe")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    throw "Cross tool not found: mipsel-none-elf-$Name.exe"
}

function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $Executable $($Arguments -join ' ')"
    }
}

$gcc = Find-Tool 'gcc'
$gxx = Find-Tool 'g++'
$objcopy = Find-Tool 'objcopy'
$objdump = Find-Tool 'objdump'
$python = (Get-Command python -ErrorAction Stop).Source
$buildRoot = Join-Path $repoRoot "build\$Target"
$objectRoot = Join-Path $buildRoot 'obj'
New-Item -ItemType Directory -Force -Path $objectRoot | Out-Null

$sources = @('src\runtime\entry.S', 'src\runtime\startup.c')
if ($Target -eq 'm0_smoke') {
    $sources += @(
        'tests\probes\m0_smoke\probe_main.c',
        'tests\probes\m0_smoke\probe_helper.c',
        'tests\probes\m0_smoke\probe_cpp.cc',
        'tests\probes\m0_smoke\probe_asm.S'
    )
    $title = 'GBA M0 Probe'
} elseif ($Target -eq 'm1_runtime') {
    $sources += @(
        'src\platform\bbk9588\cache.S',
        'tests\probes\m1_runtime\probe_main.c',
        'tests\probes\m1_runtime\probe_asm.S'
    )
    $title = 'GBA M1 Runtime'
} elseif ($Target -eq 'm1_av') {
    $sources += @('tests\probes\m1_av\probe_main.c')
    $title = 'GBA M1 AV'
} elseif ($Target -eq 'm6_save') {
    $sources += @(
        'src\libc\freestanding.c',
        'src\platform\bbk9588\save_store.c',
        'tests\probes\m6_save\probe_main.c'
    )
    $title = 'GBA M6 Save'
} else {
    if ($Target -in @('gpsp_app_interpreter', 'gpsp_app', 'gpsp_app_cpu_test')) {
        $sources += @(
            'src\platform\bbk9588\audio_output.c',
            'src\platform\bbk9588\save_store.c',
            'src\ui\gba_controls.c',
            'src\app\gpsp_app.c'
        )
    } else {
        $sources += 'src\app\gpsp_headless.c'
    }
    $sources += @(
        'src\libc\freestanding.c',
        'src\platform\bbk9588\file_stream.c',
        (Join-Path $gpspRoot 'bios_data.S'),
        (Join-Path $gpspRoot 'video.cc'),
        (Join-Path $gpspRoot 'cpu.cc'),
        (Join-Path $gpspRoot 'main.c'),
        (Join-Path $gpspRoot 'gba_memory.c'),
        (Join-Path $gpspRoot 'savestate.c'),
        (Join-Path $gpspRoot 'input.c'),
        (Join-Path $gpspRoot 'sound.c'),
        (Join-Path $gpspRoot 'cheats.c'),
        (Join-Path $gpspRoot 'serial.c'),
        (Join-Path $gpspRoot 'gbp.c'),
        (Join-Path $gpspRoot 'rfu.c'),
        (Join-Path $gpspRoot 'serial_proto.c'),
        (Join-Path $gpspRoot 'libretro\libretro.c'),
        (Join-Path $gpspRoot 'gba_cc_lut.c'),
        (Join-Path $gpspRoot 'libretro\libretro-common\compat\compat_strl.c')
    )
    if ($Target -in @('gpsp_dynarec', 'gpsp_app', 'gpsp_app_cpu_test')) {
        $patchedGpspRoot = Join-Path $buildRoot 'patched-gpsp'
        $patchedMipsRoot = Join-Path $patchedGpspRoot 'mips'
        $patchedLibretroRoot = Join-Path $patchedGpspRoot 'libretro'
        New-Item -ItemType Directory -Force -Path $patchedMipsRoot | Out-Null
        New-Item -ItemType Directory -Force -Path $patchedLibretroRoot | Out-Null
        Copy-Item -Path (Join-Path $gpspRoot '*.h') `
            -Destination $patchedGpspRoot -Force
        Copy-Item -Path (Join-Path $gpspRoot 'libretro\*.h') `
            -Destination $patchedLibretroRoot -Force
        Copy-Item -LiteralPath (Join-Path $gpspRoot 'cpu_threaded.c') `
            -Destination (Join-Path $patchedGpspRoot 'cpu_threaded.c') -Force
        foreach ($patchedSource in @(
            'gba_memory.c', 'sound.c', 'video.cc', 'libretro\libretro.c'
        )) {
            Copy-Item -LiteralPath (Join-Path $gpspRoot $patchedSource) `
                -Destination (Join-Path $patchedGpspRoot $patchedSource) -Force
        }
        Copy-Item -Path (Join-Path $gpspRoot 'mips\*') `
            -Destination $patchedMipsRoot -Recurse -Force
        $patchedGpspRelative = [IO.Path]::GetRelativePath(
            $repoRoot, $patchedGpspRoot
        ).Replace('\', '/')
        Push-Location $repoRoot
        try {
            Invoke-Checked (Get-Command git -ErrorAction Stop).Source @(
                'apply', "--directory=$patchedGpspRelative",
                (Join-Path $repoRoot 'third_party\patches\gpsp-bbk9588-dynarec.patch')
            )
        } finally {
            Pop-Location
        }
        $sourceOverrides = @{
            (Join-Path $gpspRoot 'gba_memory.c') = Join-Path $patchedGpspRoot 'gba_memory.c'
            (Join-Path $gpspRoot 'sound.c') = Join-Path $patchedGpspRoot 'sound.c'
            (Join-Path $gpspRoot 'video.cc') = Join-Path $patchedGpspRoot 'video.cc'
            (Join-Path $gpspRoot 'libretro\libretro.c') =
                Join-Path $patchedGpspRoot 'libretro\libretro.c'
        }
        $sources = @($sources | ForEach-Object {
            if ($sourceOverrides.ContainsKey($_)) {
                $sourceOverrides[$_]
            } else {
                $_
            }
        })
        $sources += @(
            'src\platform\bbk9588\cache.S',
            'src\platform\bbk9588\jit_memory.c',
            (Join-Path $patchedGpspRoot 'cpu_threaded.c'),
            (Join-Path $patchedGpspRoot 'mips\mips_stub.S')
        )
        $title = if ($Target -eq 'gpsp_app') {
            $formalGbaTitle
        } elseif ($Target -eq 'gpsp_app_cpu_test') {
            'GBA EVENT WAKE'
        } else {
            'GBA gpSP DRC'
        }
    } else {
        $title = if ($Target -eq 'gpsp_app_interpreter') {
            'GBA Emulator INT'
        } else {
            'GBA gpSP HL'
        }
    }

}

$warningFlags = @('-Wall', '-Wextra', '-Werror')
if ($Target -in @('gpsp_headless', 'gpsp_dynarec', 'gpsp_app_interpreter', 'gpsp_app', 'gpsp_app_cpu_test')) {
    $warningFlags = @('-Wall', '-Wextra')
}
$common = @(
    '-EL', '-march=mips32', '-msoft-float', '-mno-abicalls', '-G0', '-fno-pic',
    '-Os', '-ffreestanding', '-fno-builtin', '-ffunction-sections', '-fdata-sections'
) + $warningFlags + @(
    '-I', (Join-Path $sdkRoot 'sdk\include'),
    '-I', (Join-Path $repoRoot 'src'),
    '-I', (Join-Path $repoRoot 'src\libc\include')
)
if ($Target -in @('gpsp_headless', 'gpsp_dynarec', 'gpsp_app_interpreter', 'gpsp_app', 'gpsp_app_cpu_test')) {
    $common += @(
        '-DBBK9588', '-DROM_BUFFER_SIZE=2', '-DHAVE_NO_LANGEXTRA',
        '-I', $gpspRoot,
        '-I', (Join-Path $gpspRoot 'libretro'),
        '-I', (Join-Path $gpspRoot 'libretro\libretro-common\include')
    )
}
if ($Target -in @('gpsp_dynarec', 'gpsp_app', 'gpsp_app_cpu_test')) {
    $common += @(
        '-DHAVE_DYNAREC', '-DMIPS_ARCH', '-DMMAP_JIT_CACHE',
        '-DSMALL_TRANSLATION_CACHE'
    )
}
if ($Target -eq 'gpsp_app_cpu_test') {
    $common += '-DBBK_GPSP_CPU_TEST=1'
}
if ($Target -in @('gpsp_headless', 'gpsp_dynarec')) {
    $effectiveHeadlessFrames = if ($HeadlessFrames -gt 0) {
        $HeadlessFrames
    } elseif ($Target -eq 'gpsp_dynarec') {
        120
    } else {
        3
    }
    $common += "-DHEADLESS_FRAMES=$($effectiveHeadlessFrames)u"
}
$objects = @()

foreach ($relativeSource in $sources) {
    $source = if ([IO.Path]::IsPathRooted($relativeSource)) {
        $relativeSource
    } else {
        Join-Path $repoRoot $relativeSource
    }
    $objectName = ($relativeSource -replace '[\\/:]', '_') -replace '\.(c|cc|S)$', '.o'
    $object = Join-Path $objectRoot $objectName
    $extension = [IO.Path]::GetExtension($source)
    if ($extension -ceq '.cc') {
        $compiler = $gxx
        $language = @('-std=c++17', '-fno-exceptions', '-fno-rtti',
            '-fno-threadsafe-statics', '-fno-use-cxa-atexit')
    } elseif ($extension -ceq '.c') {
        $compiler = $gcc
        $language = @('-std=c11')
    } else {
        $compiler = $gcc
        $language = @('-x', 'assembler-with-cpp')
    }
    if ([IO.Path]::GetFileName($source) -eq 'bios_data.S') {
        Push-Location $gpspRoot
        try {
            Invoke-Checked $compiler @($common + $language + @('-c', $source, '-o', $object))
        } finally {
            Pop-Location
        }
    } else {
        Invoke-Checked $compiler @($common + $language + @('-c', $source, '-o', $object))
    }
    $objects += $object
}

$elf = Join-Path $buildRoot "$Target.elf"
$raw = Join-Path $buildRoot "$Target.bin"
$map = Join-Path $buildRoot "$Target.map"
$bdaName = if ($Target -eq 'gpsp_app') { "$formalGbaTitle.bda" } else { "$Target.bda" }
$bda = Join-Path $buildRoot $bdaName
$linker = Join-Path $repoRoot 'linker\bda.ld'

Invoke-Checked $gcc (@(
    '-EL', '-march=mips32', '-msoft-float', '-mno-abicalls', '-G0', '-fno-pic',
    '-nostdlib', '-Wl,--build-id=none', '-Wl,--gc-sections',
    "-Wl,-T,$linker", "-Wl,-Map,$map", '-o', $elf
) + $objects + @('-lgcc'))
Invoke-Checked $objcopy @('-O', 'binary', $elf, $raw)
Invoke-Checked $objdump @('-d', '-h', $elf) | Out-File -LiteralPath (Join-Path $buildRoot "$Target.dump.txt") -Encoding ascii
$packArguments = @(
    (Join-Path $PSScriptRoot 'pack_bda.py'), $raw,
    '--sdk', $sdkRoot,
    '--title', $title,
    '--category', '4',
    '--output', $bda
)
if ($Target -in @('gpsp_app_interpreter', 'gpsp_app', 'gpsp_app_cpu_test')) {
    $packArguments += @('--icon', (Join-Path $repoRoot 'assets\gba-icon.png'))
}
Invoke-Checked $python $packArguments

Write-Host "ELF: $elf"
Write-Host "BDA: $bda"
