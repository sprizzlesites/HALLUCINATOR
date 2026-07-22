# Windows counterpart to tools/setup_toolchain.sh.
#
# UNVERIFIED: this whole project was developed in a Linux container with no
# Windows machine available to actually run this script or build against its
# output - see README.md's Windows section. It's written from solid platform
# knowledge (Windows torch wheels are MSVC-ABI, matching a JUCE build made
# with Visual Studio - no ABI workaround needed here, unlike the Linux
# script), not from a verified run. Expect to debug it.
#
# Unlike this environment's Linux build, a normal Windows dev machine has
# unrestricted internet access, so this can just use the officially
# documented CPU wheel index instead of the --no-deps + manual CUDA-shim
# workaround tools/setup_toolchain.sh needs to route around a blocked
# download.pytorch.org.
$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

$TorchVersion = "2.4.0"

if (-not (Test-Path "libs\JUCE")) {
    git clone --depth 1 --branch 8.0.6 https://github.com/juce-framework/JUCE.git libs\JUCE
}

if (-not (Test-Path "libs\torch-venv")) {
    python -m venv libs\torch-venv
    & "libs\torch-venv\Scripts\pip.exe" install --upgrade pip
    # Genuine CPU-only wheel, no CUDA DLLs at all - this is the clean path
    # that isn't available in the sandboxed Linux dev environment (that
    # index is network-blocked there; it almost certainly isn't for you).
    & "libs\torch-venv\Scripts\pip.exe" install torch==$TorchVersion --index-url https://download.pytorch.org/whl/cpu
}

$TorchRoot = (Resolve-Path "libs\torch-venv\Lib\site-packages\torch").Path
$TorchLib = Join-Path $TorchRoot "lib"

# JUCE's own post-build step LoadLibrary()s the freshly-linked plugin DLL
# right after linking (to generate its VST3 manifest) - before this
# project's own step that copies torch's DLLs next to the plugin gets to
# run. That load fails ("The specified module could not be found") unless
# torch_cpu.dll is already discoverable some other way. Windows' DLL search
# order also checks PATH, so put torch's lib dir there up front - this is
# what actually broke the first successful CI compile+link.
$env:Path = "$TorchLib;$env:Path"

Write-Host "Toolchain ready. TORCH_ROOT = $TorchRoot"
Write-Host "Added $TorchLib to PATH for this session (re-run this script, or add it"
Write-Host "yourself, in any new shell - it doesn't persist automatically)."
Write-Host ""
Write-Host "Configure with (from a 'Developer PowerShell for VS' prompt, so cl.exe/"
Write-Host "link.exe/ninja are already on PATH regardless of which VS version you have):"
Write-Host "  cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DTORCH_ROOT=`"$TorchRoot`""
Write-Host "  cmake --build build --parallel"
Write-Host ""
Write-Host "(Using Ninja instead of -G `"Visual Studio NN YYYY`" deliberately - the exact"
Write-Host "generator string is tied to a specific VS version, and guessing wrong is"
Write-Host "exactly the failure the CI workflow hit: 'could not find any instance of"
Write-Host "Visual Studio'. Ninja + a Developer PowerShell/msvc-dev-cmd environment"
Write-Host "works regardless of which VS version is actually installed.)"
Write-Host ""
Write-Host "If download.pytorch.org/whl/cpu isn't reachable from your machine either,"
Write-Host "fall back to the same workaround tools/setup_toolchain.sh uses on Linux:"
Write-Host "  pip install torch==$TorchVersion --no-deps"
Write-Host "  # then check `pip show torch` / the wheel's METADATA for exactly which"
Write-Host "  # nvidia-*-cuXX packages it declares, and install only those - see"
Write-Host "  # tools/setup_toolchain.sh's comments for the reasoning."
