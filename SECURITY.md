# Security and private artifacts

- Never commit Wi-Fi configuration, generated headers, tokens, HMAC keys,
  flash backups, personalized UF2/BIN/PANEL files or packet captures.
- All generated builds are ignored. The repository ships source only.
- HTTP is plaintext. Keep the device on a trusted LAN or behind a VPN;
  never expose port 80 directly to the Internet.
- HMAC uses a shared secret, not a public-key signature. Anyone obtaining
  the personalized firmware may recover credentials.
- Settings, readings and OTA require the management Bearer key. The test-only
  arrow POST uses the same gate. Status and the HTML UI are public on the LAN.
- Preserve keys for updates. OTA bootloader and settings layout changes need
  special care; read the OTA documentation before replacing firmware.
- Existing example IPs and source filters describe the original installation;
  they are not access credentials and do not identify your own panel.

Do not put secrets or personalized firmware in public issues or pull requests.
