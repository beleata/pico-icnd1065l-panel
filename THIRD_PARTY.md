# Attribution and license boundaries

The repository's original code is MIT, copyright 2026 Nasko (beleata).

- `pico/boot/linker/`: adapted linker sources carry their own MIT notice,
  copyright Jakub Zimnol, in `pico/boot/linker/LICENSE`. The application linker
  also identifies its Pico SDK origin. Preserve those notices.
- Pico SDK and its libraries are external build dependencies with their own
  licenses; they are not vendored in this repository.
- ICND1065L initialization was informed by the upstream DMD_STM32/SPWM
  implementations and tested against this project's FPGA implementation.
  See historical protocol notes. This is not an official manufacturer datasheet.
- `pico/examples/*.gif` are user-supplied third-party test artwork. Original
  authors/redistribution licenses have not been established. **MIT does not
  apply to these GIFs**, including the Sonic character artwork. Substitute
  your own licensed artwork for reuse or distribution in products.
- System fonts are used locally to rasterize diagnostic text; font files are
  not included and retain their original licenses.
