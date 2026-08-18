$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Cli = 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
$Sketch = Join-Path $Root 'firmware\VRChat_CYD_Pager'
$Build = Join-Path $Root 'build\esp32'
$BuildInversionOff = Join-Path $Root 'build\esp32-inversion-off'
$Out = Join-Path $Root 'docs\firmware'
$Data = Join-Path $Root '.arduino'
$ArduinoLibraries = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Arduino\libraries'
$OneDriveLibraries = Join-Path $env:USERPROFILE 'OneDrive\Documents\Arduino\libraries'

New-Item -ItemType Directory -Force -Path $Build, $BuildInversionOff, $Out, $Data | Out-Null
$env:ARDUINO_DIRECTORIES_DATA = "$env:LOCALAPPDATA\Arduino15"
$env:ARDUINO_DIRECTORIES_DOWNLOADS = Join-Path $Data 'staging'
$env:ARDUINO_DIRECTORIES_USER = Join-Path $Data 'user'
$LibraryRoot = if (Test-Path $ArduinoLibraries) { $ArduinoLibraries } elseif (Test-Path $OneDriveLibraries) { $OneDriveLibraries } else { throw 'Arduino libraries folder not found.' }
$Fqbn = 'esp32:esp32:esp32:FlashMode=dio,FlashFreq=80,FlashSize=4M,PartitionScheme=default,CPUFreq=240,PSRAM=disabled'
& $Cli compile --fqbn $Fqbn --libraries $LibraryRoot --build-path $Build --output-dir $Build $Sketch
if ($LASTEXITCODE -ne 0) { throw 'Arduino compilation failed.' }

& $Cli compile --fqbn $Fqbn --libraries $LibraryRoot --build-path $BuildInversionOff --output-dir $BuildInversionOff --build-property 'compiler.cpp.extra_flags=-DCYD_DISPLAY_INVERSION_OFF=1' $Sketch
if ($LASTEXITCODE -ne 0) { throw 'Alternate display firmware compilation failed.' }

$Esptool = Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\esptool_py" -Recurse -Filter esptool.exe | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $Esptool) { throw 'esptool.exe was not found in the ESP32 board package.' }
$Merged = Join-Path $Out 'vrchat-cyd-pager-v0.5.2.bin'
& $Esptool.FullName --chip esp32 merge-bin -o $Merged --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 (Join-Path $Build 'VRChat_CYD_Pager.ino.bootloader.bin') 0x8000 (Join-Path $Build 'VRChat_CYD_Pager.ino.partitions.bin') 0xe000 (Join-Path $Build 'boot_app0.bin') 0x10000 (Join-Path $Build 'VRChat_CYD_Pager.ino.bin')
if ($LASTEXITCODE -ne 0) { throw 'Firmware merge failed.' }
(Get-FileHash $Merged -Algorithm SHA256).Hash.ToLowerInvariant() | Set-Content -NoNewline (Join-Path $Out 'vrchat-cyd-pager-v0.5.2.sha256')

$MergedInversionOff = Join-Path $Out 'vrchat-cyd-pager-v0.5.2-inversion-off.bin'
& $Esptool.FullName --chip esp32 merge-bin -o $MergedInversionOff --flash-mode dio --flash-freq 80m --flash-size 4MB 0x1000 (Join-Path $BuildInversionOff 'VRChat_CYD_Pager.ino.bootloader.bin') 0x8000 (Join-Path $BuildInversionOff 'VRChat_CYD_Pager.ino.partitions.bin') 0xe000 (Join-Path $BuildInversionOff 'boot_app0.bin') 0x10000 (Join-Path $BuildInversionOff 'VRChat_CYD_Pager.ino.bin')
if ($LASTEXITCODE -ne 0) { throw 'Alternate display firmware merge failed.' }
(Get-FileHash $MergedInversionOff -Algorithm SHA256).Hash.ToLowerInvariant() | Set-Content -NoNewline (Join-Path $Out 'vrchat-cyd-pager-v0.5.2-inversion-off.sha256')
Write-Host "Built $Merged"
Write-Host "Built $MergedInversionOff"
