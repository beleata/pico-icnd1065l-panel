# Four-panel static test: 0.4.3-chain4-rot180

Current change: rotate each 64x32 tile independently by 180 degrees. The panel
order, text, brightness and SCLK 1.5625 MHz remain unchanged. The preview shows
the logical text; coordinate() applies local rotation during serialization.
The previous RAM-test package is retained privately as
generated/pre-chain4-rotate.panel.

Target: four identical 64x32 ICND1065L panels connected OUT -> IN, horizontal
256x32 layout. Each panel needs external 5 V and a shared ground. The ribbon
connector does not provide panel power. GP2-GP13 and SCLK 1.5625 MHz unchanged.

This is an experimental static text test, not a finished multi-panel video
driver. User observation is required to confirm cascade order and geometry.
The text spans all panels; colored numbers 1-4 identify each 64-column region.
The assumed physical order sends the farthest panel first. If the region order
is reversed, change the panel index in generate_chain_test.py, not local pixels.

Protocol changes relative to the proven single-panel waveform:
- Register words repeat 32 times instead of 8: 512 clocks, LAT high on last 5.
- Pixel upload: 8 rows x 16 channels x 32 positions x 16 bits = 65536 clocks.
- Grayscale latch once per 512 clocks; still 128 grayscale latches per upload.
- A/B/C and OE keep their original 128-clock scan cadence, independent of LAT.
- Same 22 startup registers, 24 startup cycles, two visible uploads, then hold.
- Approximate initialization time 1.534 seconds.

All DMA sample arrays reside in SRAM. Identical 64-sample image fragments are
shared; repeated blank/hold periods use aligned 4096-byte DMA read rings.
The generated stream is tested sample-for-sample against the expanded original.
OTA/settings flash writes retain the existing DMA pause hook.
Solar reception, management authentication and OTA remain available;
automatic Solar rendering is disabled in this build only. Settings do not alter
this fixed test image. No streaming USB video support is added by this test.

Build: `./pico/build_solar.ps1 -ChainTest`
Tests: `./pico/test_solar.ps1` (including test_chain.py).
Preview: generated/chain-test-preview.png.
The build has 553984 bytes of firmware; main RAM end 0x20039ae8.
These are build measurements, not evidence of correct physical display output.

The initial 0.4.3-chain4-test flash-streaming attempt reported 105 scan faults
and did not confirm its OTA trial. The watchdog restored 0.4.2-solar-readings;
this was verified via /api/status. Do not reuse that failed flash-stream build.

Live OTA result for 0.4.3-chain4-ram: confirmed=true, backup=true,
updating=false, errors=0. Physical output still needs user confirmation.

Recovery: the private pre-test signed Solar image was saved as
generated/pre-chain4-solar.panel. Upload it with panel_admin.py (pace requests
for the single-connection HTTP server). The successful test OTA also retains
the previous application in its rollback slot. Building without -ChainTest
explicitly resets PICO_PANEL_CHAIN_TEST=OFF and restores normal Solar rendering.
Private packages contain credentials and must not be published.
