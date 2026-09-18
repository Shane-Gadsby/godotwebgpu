<#
.SYNOPSIS
    Native Windows build script for Godot WebGPU: installs prerequisites
    (Visual Studio Build Tools, Python, SCons, D3D12/ANGLE/AccessKit deps,
    Emscripten) then builds the editor, the native Windows export templates,
    and the web/WebGPU export templates.

.DESCRIPTION
    Mirrors this repo's own CI pipeline (.github/workflows/windows_builds.yml):
    Python + SCons via pip, then misc/scripts/install_d3d12_sdk_windows.py,
    install_angle.py, install_winrt.py, install_accesskit.py to fetch
    pre-built dependencies, then scons platform=windows for editor,
    template_debug, and template_release. Also installs Emscripten (emsdk)
    and builds scons platform=web webgpu=yes template_debug/template_release,
    since that's this fork's actual export target. Run this FROM the repo
    root, in PowerShell, ON Windows (this is not for cross-compiling from
    Linux -- see build.sh for that).

.PARAMETER SkipInstall
    Skip all dependency installation (winget packages, pip, fetch scripts,
    emsdk) and go straight to building. Use this on a machine already set up.

.PARAMETER SkipWeb
    Skip installing Emscripten and building the web/WebGPU export template;
    only build the native Windows editor + templates.

.PARAMETER Jobs
    Parallel build jobs for scons (-j). Defaults to the logical processor count.

.PARAMETER SconsVersion
    pip-installed SCons version. Defaults to 4.10.1 (matches this repo's CI).

.PARAMETER EmsdkDir
    Emscripten SDK location. Defaults to "$HOME\emsdk".

.PARAMETER EmsdkVersion
    Emscripten version to install/activate. Defaults to 6.0.9.

.EXAMPLE
    .\build-windows.ps1
    Installs everything needed, then builds editor + native templates + web/WebGPU templates.

.EXAMPLE
    .\build-windows.ps1 -SkipInstall -Jobs 16
    Skips dependency setup (already done) and just builds.
#>

[CmdletBinding()]
param(
    [switch]$SkipInstall,
    [switch]$SkipWeb,
    [int]$Jobs = 0,
    [string]$SconsVersion = "4.10.1",
    [string]$EmsdkDir = (Join-Path $HOME "emsdk"),
    [string]$EmsdkVersion = "6.0.9"
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
        [Parameter(ValueFromRemainingArguments)][string[]]$ArgList
    )
    & $Exe @ArgList
    if ($LASTEXITCODE -ne 0) {
        throw "'$Exe $($ArgList -join ' ')' exited with code $LASTEXITCODE"
    }
}

function Test-Cmd($name) {
    return [bool](Get-Command $name -ErrorAction SilentlyContinue)
}

function Install-Emsdk {
    Write-Step "Setting up Emscripten SDK ($EmsdkVersion) at $EmsdkDir..."
    try {
        if (-not (Test-Path $EmsdkDir)) {
            Invoke-Native git clone https://github.com/emscripten-core/emsdk.git $EmsdkDir
        } else {
            Push-Location $EmsdkDir
            try { Invoke-Native git pull --ff-only } catch { Write-Warn "Could not update existing emsdk checkout at $EmsdkDir, using it as-is." }
            Pop-Location
        }
        Push-Location $EmsdkDir
        try {
            Invoke-Native .\emsdk install $EmsdkVersion
            Invoke-Native .\emsdk activate $EmsdkVersion
        } finally {
            Pop-Location
        }
    } catch {
        Write-Warn "emsdk install/activate failed ($_) -- skipping the web/WebGPU template build."
        $script:SkipWeb = $true
    }
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
        } catch {
            # `python --version` failed or gave unparsable output -- fall
            # through with $needPython still true and reinstall.
            Write-Verbose "Could not parse python version: $_"
        }
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
    if (-not (Test-Cmd "scons")) {
        # pip falls back to a --user install when normal site-packages isn't
        # writable, which drops scons.exe in the per-user Scripts dir --
        # that dir isn't guaranteed to be on PATH (pip warns about this).
        # `site --user-base` omits the version subdir (e.g. Python314) that
        # pip actually installs scripts into, so ask sysconfig directly.
        $userScriptsDir = python -c "import sysconfig, os; print(sysconfig.get_path('scripts', f'{os.name}_user'))"
        if (Test-Path $userScriptsDir) {
            Write-Warn "scons not on PATH after pip install -- adding $userScriptsDir for this session."
            $env:Path = "$userScriptsDir;$env:Path"
        }
    }
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

    if (-not $SkipWeb) {
        Install-Emsdk
    }
} else {
    Write-Warn "Skipping dependency installation (-SkipInstall). Assuming scons/MSVC/D3D12/ANGLE/AccessKit/Emscripten are already set up."
    $d3d12Enabled = "yes"
    $angleEnabled = "yes"
    $accesskitEnabled = "yes"
    if (-not $SkipWeb -and -not (Test-Path (Join-Path $EmsdkDir "emsdk_env.ps1"))) {
        Write-ErrAndExit "Emscripten not found at $EmsdkDir (pass -EmsdkDir, or -SkipWeb to skip the web template build)."
    }
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

# Packages whatever template dirs currently exist under $OutDir (templates\,
# templates_linux\, templates_windows\, templates_macos\ -- useful if this is
# a shared builds\ dir with output from build.sh's cross-builds too) into a
# single .tpz -- the format Godot's Export Template Manager
# (editor/export/export_template_manager.cpp's _tpz_file_selected) expects:
# a zip containing one "templates\" directory holding version.txt (the raw
# version string) alongside every template file, flattened.
function New-TpzPackage {
    $staging = Join-Path $OutDir ".tpz_staging"
    if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
    $templatesDir = Join-Path $staging "templates"
    New-Item -ItemType Directory -Force -Path $templatesDir | Out-Null

    $found = $false
    foreach ($sub in @("templates", "templates_linux", "templates_windows", "templates_macos")) {
        $srcDir = Join-Path $OutDir $sub
        if (Test-Path $srcDir) {
            Get-ChildItem -Path $srcDir -File | ForEach-Object {
                Copy-Item $_.FullName (Join-Path $templatesDir $_.Name) -Force
                $found = $true
            }
        }
    }

    if (-not $found) {
        Remove-Item $staging -Recurse -Force
        return
    }

    Set-Content -Path (Join-Path $templatesDir "version.txt") -Value $Version -NoNewline

    $tpz = Join-Path $OutDir "godot-webgpu-export-templates-$Version.tpz"
    $zipTemp = "$tpz.zip"
    if (Test-Path $tpz) { Remove-Item $tpz -Force }
    if (Test-Path $zipTemp) { Remove-Item $zipTemp -Force }
    Compress-Archive -Path $templatesDir -DestinationPath $zipTemp
    Move-Item $zipTemp $tpz
    Remove-Item $staging -Recurse -Force
    Write-Ok "Export template package -> $tpz"
}

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
Copy-BuiltExe -Pattern "godot.windows.editor.dev.x86_64*.exe" -Dest (Join-Path $OutDir "editor_windows")
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

if (-not $SkipWeb) {
    Write-Step "Activating Emscripten environment..."
    . (Join-Path $EmsdkDir "emsdk_env.ps1")

    Write-Step "Building web/WebGPU export template (debug)..."
    Invoke-Native scons platform=web target=template_debug dlink_enabled=yes webgpu=yes opengl3=no threads=no -j $Jobs

    Write-Step "Building web/WebGPU export template (release)..."
    Invoke-Native scons platform=web target=template_release dlink_enabled=yes webgpu=yes opengl3=no threads=no -j $Jobs

    $webDest = Join-Path $OutDir "templates"
    New-Item -ItemType Directory -Force -Path $webDest | Out-Null
    $debugZip = Get-ChildItem -Path (Join-Path $RepoRoot "bin") -Filter "godot.web.template_debug*.zip" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $releaseZip = Get-ChildItem -Path (Join-Path $RepoRoot "bin") -Filter "godot.web.template_release*.zip" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $debugZip -or -not $releaseZip) {
        throw "Web template build succeeded but a .zip wasn't found in bin\."
    }
    Copy-Item $debugZip.FullName (Join-Path $webDest "web_nothreads_debug.zip") -Force
    Copy-Item $releaseZip.FullName (Join-Path $webDest "web_nothreads_release.zip") -Force
    Write-Ok "Web templates -> $webDest"
} else {
    Write-Warn "Skipping web/WebGPU template build (-SkipWeb)."
}

New-TpzPackage

Write-Host "`nDone. Output in builds\$Version\" -ForegroundColor Green
