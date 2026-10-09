param([string]$SolarRoot='C:/Users/user/Documents/ChatGPT/Solar')
$ErrorActionPreference='Stop'
$zig=Join-Path $env:USERPROFILE '.cache/pico-panel/host-tools/ziglang/zig.exe'
if(-not (Test-Path $zig)) { throw 'Install host ziglang into .cache/pico-panel/host-tools or compile test_solar_native.cpp with a C++17 compiler' }
$runner=Join-Path $PSScriptRoot 'generated/test_solar_native.exe'
& $zig c++ -std=c++17 -O1 -fno-threadsafe-statics -nostdlib++ "$PSScriptRoot/test_arrow_native.cpp" -o "$PSScriptRoot/generated/test_arrow_native.exe"
if($LASTEXITCODE) { throw 'Arrow native test build failed' }
& "$PSScriptRoot/generated/test_arrow_native.exe"
if($LASTEXITCODE) { throw 'Arrow waveform test failed' }
& $zig c++ -std=c++17 -O1 -fno-threadsafe-statics -nostdlib++ "$PSScriptRoot/test_readings_native.cpp" -o "$PSScriptRoot/generated/test_readings_native.exe"
if($LASTEXITCODE) { throw 'Readings native test build failed' }
& "$PSScriptRoot/generated/test_readings_native.exe"
if($LASTEXITCODE) { throw 'Readings cache test failed' }
& $zig c++ -std=c++17 -O1 -fno-threadsafe-statics -nostdlib++ "$PSScriptRoot/test_solar_native.cpp" -o $runner
if($LASTEXITCODE) { throw 'Native test build failed' }
python "$PSScriptRoot/test_solar_integration.py" --solar $SolarRoot --runner $runner
if($LASTEXITCODE) { throw 'Solar differential test failed' }
& $zig c++ -std=c++17 -O1 -fno-threadsafe-statics -nostdlib++ "$PSScriptRoot/test_ota_native.cpp" -o "$PSScriptRoot/generated/test_ota_native.exe"
if($LASTEXITCODE) { throw 'OTA native test build failed' }
& "$PSScriptRoot/generated/test_ota_native.exe"
if($LASTEXITCODE) { throw 'OTA power-cut test failed' }
python -m unittest discover -s $PSScriptRoot -p 'test_*.py' -v
if($LASTEXITCODE) { throw 'Panel regression tests failed' }
