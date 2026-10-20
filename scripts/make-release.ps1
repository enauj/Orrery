# Builds Orrery in Release and packs CLAP, VST3 and Standalone into a zip to share.
#
#   powershell -ExecutionPolicy Bypass -File scripts\make-release.ps1
#
# The version comes from project(... VERSION x.y.z) in CMakeLists.txt.
# Output: dist\Orrery-<version>-Windows.zip

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$version = [regex]::Match((Get-Content CMakeLists.txt -Raw), 'project\(\w+ VERSION ([\d.]+)').Groups[1].Value
if (-not $version) { throw 'Could not read the version from CMakeLists.txt' }
$name = "Orrery-$version-Windows"

# Configure if needed, then build the three formats.
if (-not (Test-Path build\CMakeCache.txt)) {
    cmake -S . -B build
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
}
cmake --build build --config Release --target ModLanes_CLAP ModLanes_VST3 ModLanes_Standalone
if ($LASTEXITCODE -ne 0) { throw 'Build failed (close Bitwig / the Standalone if a file is locked)' }

$artefacts = 'build\ModLanes_artefacts\Release'
$stage = "dist\$name"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force "$stage\CLAP", "$stage\VST3", "$stage\Standalone" | Out-Null

Copy-Item "$artefacts\CLAP\Orrery.clap" "$stage\CLAP\"
Copy-Item "$artefacts\VST3\Orrery.vst3" "$stage\VST3\" -Recurse
Copy-Item "$artefacts\Standalone\Orrery.exe" "$stage\Standalone\"
Copy-Item README.md "$stage\README.md"

@"
Orrery $version for Windows (64-bit)
====================================

Install the format your DAW uses (you can install several):

  CLAP (Bitwig, Reaper, ...)
      Copy CLAP\Orrery.clap to   C:\Program Files\Common Files\CLAP
      (or, without admin rights, %LOCALAPPDATA%\Programs\Common\CLAP)

  VST3 (Ableton Live, Cubase, FL Studio, Reaper, ...)
      Copy the whole VST3\Orrery.vst3 folder to   C:\Program Files\Common Files\VST3

  Standalone
      Run Standalone\Orrery.exe directly, no install needed.

Then rescan plugins in your DAW. Orrery is an audio effect that sends MIDI CC:
put it on a track and route its MIDI output to the instrument you want to modulate
(see README.md for details).

Windows may warn that the app is from an unknown publisher, because it is not
code-signed. Click "More info" -> "Run anyway" for the Standalone. If the plugin
files are blocked, right-click them -> Properties -> tick "Unblock".
"@ | Set-Content "$stage\INSTALL.txt" -Encoding utf8

$zip = "dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
# Entries are added one by one with '/' separators: Compress-Archive and ZipFile in
# Windows PowerShell 5.1 write '\', which some unzip tools mishandle.
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$stageFull = (Resolve-Path $stage).Path
$archive = [IO.Compression.ZipFile]::Open((Join-Path (Resolve-Path dist) "$name.zip"), 'Create')
try {
    Get-ChildItem $stageFull -Recurse -File | ForEach-Object {
        $entry = $_.FullName.Substring($stageFull.Length + 1).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $_.FullName, $entry, 'Optimal') | Out-Null
    }
}
finally { $archive.Dispose() }
Remove-Item $stage -Recurse -Force

$size = '{0:N1} MB' -f ((Get-Item $zip).Length / 1MB)
Write-Host "Release ready: $zip ($size)"
