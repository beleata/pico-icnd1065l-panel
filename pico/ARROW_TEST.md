# Four-panel arrow diagnostic (0.4.4-chain4-arrow)

Build with `./pico/build_solar.ps1 -ArrowTest`. Retains Pico W networking,
authentication and OTA recovery, 1.5625 MHz SCLK and per-panel 180-degree
rotation. Starts with a black screen and waits for an authenticated command:

`POST /api/test/arrow` with an empty body and the management Bearer token.
This test-only endpoint is absent from normal Solar and static-chain builds.

The white arrow is initially in logical panel 1 (left edge x=14). It holds for
2.5 seconds after upload, moves in 8-pixel steps through the 256x32 display,
then leaves panel 4 and stays black. A subsequent POST repeats the one-shot
sequence. Physical ordering/direction still require visual confirmation.

Each position is uploaded twice with the existing no-config refresh prefix
and hold intervals. It does not reinitialize registers per animation frame.
DMA uses 16 KiB of SRAM waveform templates and reuses the startup descriptor
array only after startup completes. The full raw four-panel waveform is never
allocated. No live DMA-read memory is overwritten while an upload runs.

Tests: test_arrow_native.cpp checks template clock/address/OE/LAT timing and
complete disappearance; test_chain.py checks rotation/mapping and compacted
startup waveform equivalence. Existing Solar and OTA tests remain enabled.
Main RAM end in this build: 0x20038aec. Firmware binary: 534528 bytes.

Private recovery image: generated/pre-arrow-rot180.panel. The original Solar
image remains in generated/pre-chain4-solar.panel. Do not publish private images.
