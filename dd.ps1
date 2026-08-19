<#
.SYNOPSIS
    Noterad build helper.

.DESCRIPTION
    dd run   - build Release x64 and start the app
    dd build - build Release x64
    dd test  - build Release x64 and run the unit tests, output to the console
    dd clean - remove build/ and exe/

    Pass -Config Debug to build a command's target as Debug instead.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('run', 'build', 'test', 'clean')]
    [string]$Command = 'run',

    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$exeSuffix = if ($Config -eq 'Debug') { '64d' } else { '64' }

function Get-VisualStudioPath {
    $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe was not found; install Visual Studio with the C++ desktop workload.'
    }

    $path = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
        Select-Object -First 1

    if (-not $path) {
        throw 'No Visual Studio installation with the C++ desktop workload was found.'
    }

    return $path
}

# cl and rc only work inside the MSVC environment, and Ninja invokes cl directly.
function Enter-MsvcEnvironment {
    param([string] $VisualStudio)

    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64') {
        return
    }

    $vcvars = Join-Path $VisualStudio 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) {
        throw "vcvars64.bat was not found at $vcvars."
    }

    & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            Set-Item -LiteralPath "Env:$($Matches[1])" -Value $Matches[2]
        }
    }
}

# Prefer whatever is on PATH, then the copy Visual Studio ships, so neither tool
# has to be installed separately.
function Resolve-Tool {
    param([string] $Name, [string] $VisualStudio, [string] $BundledRelativePath)

    $onPath = Get-Command $Name -ErrorAction SilentlyContinue
    if ($onPath) {
        return $onPath.Source
    }

    $bundled = Join-Path $VisualStudio $BundledRelativePath
    if (Test-Path $bundled) {
        return $bundled
    }

    throw "$Name was not found on PATH or under $VisualStudio."
}

function Invoke-Build([string]$Configuration) {
    $vs = Get-VisualStudioPath
    Enter-MsvcEnvironment -VisualStudio $vs

    $cmake = Resolve-Tool -Name 'cmake' -VisualStudio $vs `
        -BundledRelativePath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    $ninja = Resolve-Tool -Name 'ninja' -VisualStudio $vs `
        -BundledRelativePath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'

    $env:PATH = "$(Split-Path $ninja);$env:PATH"

    # The linker fails with LNK1168 if the previous build is still running.
    Get-Process noterad-64, noterad-64d -ErrorAction SilentlyContinue | Stop-Process -Force

    $preset = $Configuration.ToLowerInvariant()

    Push-Location $root
    try {
        Write-Host "Building $Configuration x64..." -ForegroundColor Cyan
        & $cmake --preset $preset
        if ($LASTEXITCODE -ne 0) { throw "Configure failed ($Configuration)." }

        & $cmake --build --preset $preset
        if ($LASTEXITCODE -ne 0) { throw "Build failed ($Configuration)." }
    }
    finally {
        Pop-Location
    }
}

switch ($Command) {
    'build' { Invoke-Build $Config }

    'clean' {
        Get-Process noterad-64, noterad-64d -ErrorAction SilentlyContinue | Stop-Process -Force
        foreach ($path in @('build', 'exe')) {
            $full = Join-Path $root $path
            if (Test-Path $full) { Remove-Item -LiteralPath $full -Recurse -Force }
        }
    }

    'run' {
        Invoke-Build $Config
        $exe = Join-Path $root "exe\noterad-$exeSuffix.exe"
        Write-Host "Starting $exe" -ForegroundColor Cyan
        if ($Rest) { Start-Process -FilePath $exe -ArgumentList $Rest } else { Start-Process -FilePath $exe }
    }

    'test' {
        Invoke-Build $Config
        $exe = Join-Path $root "exe\noterad-$exeSuffix.exe"
        $tmp = Join-Path $root 'tmp'
        if (-not (Test-Path $tmp)) { New-Item -ItemType Directory -Path $tmp | Out-Null }
        $out = Join-Path $tmp 'test_out.txt'
        $err = Join-Path $tmp 'test_err.txt'

        # GUI subsystem app: redirect to files so PowerShell waits and captures the output.
        $p = Start-Process -FilePath $exe -ArgumentList '/test' -Wait -PassThru -NoNewWindow `
            -RedirectStandardOutput $out -RedirectStandardError $err
        if (Test-Path $out) { Get-Content $out }
        if ((Test-Path $err) -and (Get-Item $err).Length -gt 0) { Get-Content $err | Write-Host -ForegroundColor Red }

        if ($p.ExitCode -ne 0) {
            Write-Host "Tests FAILED (exit $($p.ExitCode))" -ForegroundColor Red
        }
        else {
            Write-Host 'Tests passed' -ForegroundColor Green
        }
        exit $p.ExitCode
    }
}
