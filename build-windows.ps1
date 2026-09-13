<#
.SYNOPSIS
    Native Windows build script for Godot WebGPU: installs prerequisites
    (Visual Studio Build Tools, Python, SCons, D3D12/ANGLE/AccessKit deps)
    then builds the editor and export templates.

.DESCRIPTION
    Mirrors this repo's own CI pipeline (.github/workflows/windows_builds.yml):
    Python + SCons via pip, then misc/scripts/install_d3d12_sdk_windows.py,
    install_angle.py, install_winrt.py, install_accesskit.py to fetch
    pre-built dependencies, then scons platform=windows for editor,
    template_debug, and template_release. Run this FROM the repo root, in
    PowerShell, ON Windows (this is not for cross-compiling from Linux --
    see build.sh for that).

.PARAMETER SkipInstall
    Skip all dependency installation (winget packages, pip, fetch scripts)
    and go straight to building. Use this on a machine already set up.

.PARAMETER Jobs
    Parallel build jobs for scons (-j). Defaults to the logical processor count.

.PARAMETER SconsVersion
    pip-installed SCons version. Defaults to 4.10.1 (matches this repo's CI).

.EXAMPLE
    .\build-windows.ps1
    Installs everything needed, then builds editor + both templates.

.EXAMPLE
    .\build-windows.ps1 -SkipInstall -Jobs 16
    Skips dependency setup (already done) and just builds.
#>

[CmdletBinding()]
param(
    [switch]$SkipInstall,
    [int]$Jobs = 0,
    [string]$SconsVersion = "4.10.1"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $RepoRoot

function Write-Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Write-Ok($msg) { Write-Host $msg -ForegroundColor Green }
function Write-Warn($msg) { Write-Host $msg -ForegroundColor Yellow }
function Write-ErrAndExit($msg) {
    Write-Host $msg -ForegroundColor Red
    exit 1
}

function Invoke-Native {
    # Runs a native command and throws if it exits non-zero -- PowerShell
    # does not do this on its own for external executables.
    param(
        [Parameter(Mandatory)][string]$Exe,
        [Parameter(ValueFromRemainingArguments)][string[]]$Args
    )
    & $Exe @Args
    if ($LASTEXITCODE -ne 0) {
        throw "'$Exe $($Args -join ' ')' exited with code $LASTEXITCODE"
    }
}

function Test-Cmd($name) {
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

# --- Step 0: winget itself ---------------------------------------------

if (-not $SkipInstall -and -not (Test-Cmd "winget")) {
    Write-ErrAndExit "winget not found. Install 'App Installer' from the Microsoft Store or https://aka.ms/getwinget, then re-run this script."
}

function Install-WingetPackage {
    param(
        [Parameter(Mandatory)][string]$Id,
        [string]$Override
    )
    Write-Step "Installing $Id via winget..."
    $wingetArgs = @("install", "--id", $Id, "-e", "--silent", "--accept-package-agreements", "--accept-source-agreements")
    if ($Override) {
        $wingetArgs += @("--override", $Override)
    }
    & winget @wingetArgs
    # winget returns non-zero (APPINSTALLER_CLI_ERROR_NO_APPLICABLE_UPGRADE,
    # roughly -1978335212) when the package is already installed at the
    # latest version -- that's success, not failure, for our purposes.
    if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne -1978335212) {
        throw "winget install failed for $Id (exit code $LASTEXITCODE). If this is Visual Studio Build Tools, try re-running from an elevated (Administrator) PowerShell."
    }
}

if (-not $SkipInstall) {
    # --- Step 1: git + submodules ---------------------------------------

    Write-Step "Checking for git..."
    if (-not (Test-Cmd "git")) {
        Install-WingetPackage -Id "Git.Git"
        $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
    }
    if (Test-Path (Join-Path $RepoRoot ".git")) {
        Write-Step "Updating git submodules..."
        Invoke-Native git submodule update --init --recursive
    }

    # --- Step 2: Python ---------------------------------------------------

    Write-Step "Checking for Python..."
    $needPython = $true
    if (Test-Cmd "python") {
        try {
            $verOut = (python --version 2>&1).ToString()
            if ($verOut -match "Python (\d+)\.(\d+)") {
                $maj = [int]$Matches[1]; $min = [int]$Matches[2]
                if ($maj -gt 3 -or ($maj -eq 3 -and $min -ge 8)) {
                    $needPython = $false
                }
            }
        } catch {}
    }
    if ($needPython) {
        Install-WingetPackage -Id "Python.Python.3.12"
        $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path", "User")
        if (-not (Test-Cmd "python")) {
            Write-ErrAndExit "Python was installed but isn't on PATH yet. Open a new PowerShell window and re-run this script."
        }
    }
    Write-Ok "Python OK: $(python --version)"

    Write-Step "Installing SCons $SconsVersion..."
    Invoke-Native python -m pip install --upgrade pip
    Invoke-Native python -m pip install "scons==$SconsVersion"
    Invoke-Native scons --version

    # --- Step 3: MSVC Build Tools (C++ workload) --------------------------

    Write-Step "Checking for MSVC C++ build tools..."
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $hasVCTools = $false
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($vsPath) { $hasVCTools = $true }
    }
    if (-not $hasVCTools) {
        Write-Warn "MSVC C++ build tools not found -- installing Visual Studio 2022 Build Tools."
        Write-Warn "This is a multi-GB download and may prompt for elevation (UAC)."
        Install-WingetPackage -Id "Microsoft.VisualStudio.2022.BuildTools" `
            -Override "--quiet --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    } else {
        Write-Ok "MSVC C++ build tools found at $vsPath"
    }

    # --- Step 4: pre-built dependencies (D3D12 / ANGLE / WinRT / AccessKit) --

    Write-Step "Fetching Direct3D 12 SDK components..."
    python misc\scripts\install_d3d12_sdk_windows.py
    $d3d12Enabled = if ($LASTEXITCODE -eq 0) { "yes" } else { Write-Warn "D3D12 SDK install failed, building with d3d12=no."; "no" }

    Write-Step "Fetching pre-built ANGLE..."
    python misc\scripts\install_angle.py
    $angleEnabled = if ($LASTEXITCODE -eq 0) { "yes" } else { Write-Warn "ANGLE install failed, building with angle=no."; "no" }

    Write-Step "Fetching WinRT components..."
    python misc\scripts\install_winrt.py
    if ($LASTEXITCODE -ne 0) { Write-Warn "WinRT component install failed (non-fatal, continuing)." }

    Write-Step "Fetching pre-built AccessKit..."
    python misc\scripts\install_accesskit.py
    $accesskitEnabled = if ($LASTEXITCODE -eq 0) { "yes" } else { Write-Warn "AccessKit install failed, building with accesskit=no."; "no" }
} else {
    Write-Warn "Skipping dependency installation (-SkipInstall). Assuming scons/MSVC/D3D12/ANGLE/AccessKit are already set up."
    $d3d12Enabled = "yes"
    $angleEnabled = "yes"
    $accesskitEnabled = "yes"
}

# --- Step 5: figure out -j and the output version string -----------------

if ($Jobs -le 0) {
    $Jobs = [Environment]::ProcessorCount
}

$versionPyCode = @'
import sys
ns = {}
with open(sys.argv[1]) as f:
    exec(f.read(), ns)
parts = [str(ns["major"]), str(ns["minor"])]
if ns.get("patch"):
    parts.append(str(ns["patch"]))
print(".".join(parts) + "." + ns["status"])
'@
$Version = ($versionPyCode | python - (Join-Path $RepoRoot "version.py")).Trim()
$OutDir = Join-Path $RepoRoot "builds\$Version"

Write-Step "Godot WebGPU Windows build -- version $Version, -j$Jobs"

# --- Step 6: build editor + both templates --------------------------------

function Copy-BuiltExe {
    param(
        [Parameter(Mandatory)][string]$Pattern,
        [Parameter(Mandatory)][string]$Dest,
        [hashtable]$Rename
    )
    New-Item -ItemType Directory -Force -Path $Dest | Out-Null
    $found = Get-ChildItem -Path (Join-Path $RepoRoot "bin") -Filter $Pattern -File -ErrorAction SilentlyContinue
    if (-not $found) {
        throw "Build succeeded but no file matching '$Pattern' was found in bin\."
    }
    foreach ($f in $found) {
        $destName = $f.Name
        if ($Rename -and $Rename.ContainsKey($f.Name)) {
            $destName = $Rename[$f.Name]
        }
        Copy-Item $f.FullName (Join-Path $Dest $destName) -Force
    }
}

Write-Step "Building editor (platform=windows, dev_build=yes)..."
Invoke-Native scons platform=windows target=editor dev_build=yes `
    d3d12=$d3d12Enabled accesskit=$accesskitEnabled angle=$angleEnabled -j $Jobs
Copy-BuiltExe -Pattern "godot.windows.editor.x86_64*.exe" -Dest (Join-Path $OutDir "editor_windows")
Write-Ok "Editor -> $OutDir\editor_windows"

Write-Step "Building export template (debug)..."
Invoke-Native scons platform=windows target=template_debug `
    d3d12=$d3d12Enabled accesskit=$accesskitEnabled angle=$angleEnabled -j $Jobs

Write-Step "Building export template (release)..."
Invoke-Native scons platform=windows target=template_release `
    d3d12=$d3d12Enabled accesskit=$accesskitEnabled angle=$angleEnabled -j $Jobs

# Renamed to match the filenames Godot's export_templates directory expects
# (~\AppData\Roaming\Godot\export_templates\$Version\), same convention as
# build.sh's web/macOS/Windows template output.
Copy-BuiltExe -Pattern "godot.windows.template_debug.x86_64.exe" -Dest (Join-Path $OutDir "templates_windows") `
    -Rename @{ "godot.windows.template_debug.x86_64.exe" = "windows_debug_x86_64.exe" }
Copy-BuiltExe -Pattern "godot.windows.template_debug.x86_64.console.exe" -Dest (Join-Path $OutDir "templates_windows") `
    -Rename @{ "godot.windows.template_debug.x86_64.console.exe" = "windows_debug_x86_64_console.exe" }
Copy-BuiltExe -Pattern "godot.windows.template_release.x86_64.exe" -Dest (Join-Path $OutDir "templates_windows") `
    -Rename @{ "godot.windows.template_release.x86_64.exe" = "windows_release_x86_64.exe" }
Copy-BuiltExe -Pattern "godot.windows.template_release.x86_64.console.exe" -Dest (Join-Path $OutDir "templates_windows") `
    -Rename @{ "godot.windows.template_release.x86_64.console.exe" = "windows_release_x86_64_console.exe" }
Write-Ok "Templates -> $OutDir\templates_windows"

Write-Host "`nDone. Output in builds\$Version\" -ForegroundColor Green
