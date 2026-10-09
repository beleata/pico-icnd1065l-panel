param([string]$Version='0.4.2-solar-readings',[switch]$TestCrash,[switch]$ChainTest,[switch]$ArrowTest)
if($ArrowTest) { $ChainTest=$true; $Version='0.4.4-chain4-arrow' }
$ErrorActionPreference='Stop'
$tools=Join-Path $env:USERPROFILE '.cache/pico-panel'
$cmake=Join-Path $tools 'python/cmake/data/bin/cmake.exe'
$compiler=Get-ChildItem "$tools/arm" -Filter arm-none-eabi-gcc.exe -Recurse | Select-Object -First 1
if(-not $compiler) { throw 'Arm compiler missing' }
if(-not (Test-Path "$PSScriptRoot/generated/solar_wifi.h")) { throw 'Run prepare_solar_wifi.py first' }
$env:PICO_SDK_PATH="$tools/pico-sdk"
$env:PICO_TOOLCHAIN_PATH=Split-Path $compiler.DirectoryName
$env:PATH="$($compiler.DirectoryName);$env:PATH"
python "$PSScriptRoot/generate_wave.py"
if($LASTEXITCODE) { throw 'Wave generation failed' }
python "$PSScriptRoot/ota_package.py"
if($LASTEXITCODE) { throw 'OTA preparation failed' }
& $cmake -S $PSScriptRoot -B "$PSScriptRoot/generated/build-solar" -G Ninja "-DCMAKE_MAKE_PROGRAM=$tools/python/bin/ninja.exe" '-DPICO_BOARD=pico_w' "-Dpioasm_DIR=$tools/pico-sdk-tools-2.3.1-x64-win/pioasm" "-Dpicotool_DIR=$tools/picotool-2.3.1-x64-win/picotool" '-DCMAKE_BUILD_TYPE=Release' '-DPICO_PANEL_SOLAR=ON' '-DPICO_PANEL_REFRESH_NO_CONFIG=ON' '-DPICO_PANEL_REFRESH_NO_GAP=ON' '-DPICO_PANEL_REFRESH_CONTINUOUS=OFF'
if($LASTEXITCODE) { throw 'CMake failed' }
$crash=if($TestCrash){'ON'}else{'OFF'}
$chain=if($ChainTest){'ON'}else{'OFF'}
if($ChainTest) {
    if($Version -eq '0.4.2-solar-readings') { $Version='0.4.3-chain4-rot180' }
    if($ArrowTest) { python "$PSScriptRoot/generate_chain_test.py" --blank }
    else { python "$PSScriptRoot/generate_chain_test.py" }
    if($LASTEXITCODE) { throw 'Chain test generation failed' }
}
$arrow=if($ArrowTest){'ON'}else{'OFF'}
& $cmake -S $PSScriptRoot -B "$PSScriptRoot/generated/build-solar" "-DPANEL_VERSION=$Version" "-DPANEL_OTA_TEST_CRASH=$crash" "-DPICO_PANEL_CHAIN_TEST=$chain" "-DPICO_PANEL_CHAIN_ARROW=$arrow"
if($LASTEXITCODE) { throw 'Version configuration failed' }
& $cmake --build "$PSScriptRoot/generated/build-solar" -j 8
if($LASTEXITCODE) { throw 'Build failed' }
python "$PSScriptRoot/ota_package.py" --binary "$PSScriptRoot/generated/build-solar/pico_panel.bin" --revision $Version
if($LASTEXITCODE) { throw 'OTA packaging failed' }
Write-Output 'PRIVATE firmware: generated/build-solar/pico_panel.uf2 contains Wi-Fi credentials. Do not publish.'
