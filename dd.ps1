<#
.SYNOPSIS
    Noterad build helper.

.DESCRIPTION
    dd run   - build Release x64 and start the app
    dd test  - build Debug x64 and run the unit tests, output to the console
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('run', 'test')]
    [string]$Command = 'run',

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

function Get-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -prerelease -products * `
            -requires Microsoft.Component.MSBuild `
            -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        if ($found) { return $found }
    }

    $cmd = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    throw 'MSBuild.exe not found. Install Visual Studio or run from a Developer prompt.'
}

function Invoke-Build([string]$Configuration) {
    $msbuild = Get-MSBuild
    Write-Host "Building $Configuration x64..." -ForegroundColor Cyan
    & $msbuild (Join-Path $root 'noterad.sln') "/p:Configuration=$Configuration" /p:Platform=x64 /m /nologo /v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($Configuration)." }
}

switch ($Command) {
    'run' {
        Invoke-Build 'Release'
        $exe = Join-Path $root 'exe\noterad-64.exe'
        Write-Host "Starting $exe" -ForegroundColor Cyan
        if ($Rest) { Start-Process -FilePath $exe -ArgumentList $Rest } else { Start-Process -FilePath $exe }
    }

    'test' {
        Invoke-Build 'Debug'
        $exe = Join-Path $root 'exe\noterad-64d.exe'
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
