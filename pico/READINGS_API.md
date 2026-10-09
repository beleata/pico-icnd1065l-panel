# Cached Solar readings (0.4.2-solar-readings)

GET `/api/readings` requires the same `Authorization: Bearer <management key>`
as settings and OTA. Missing or invalid authorization returns HTTP 401 with
`Invalid management key`. There is no anonymous measurements endpoint.

The handler reads the existing in-memory Solar state without networking,
flash writes, allocation, settings changes, or stopping display scanning.
Its response uses a static 384-byte buffer; formatting checks truncation.

Fields:
- `ts`: timestamp carried by the latest site (unit 100) report, or null.
  This is the source's raw Unix-like seconds value, not independently verified
  UTC and not the request time. Battery reports have separate timestamps.
- `solar_power`, `consume_power`: site PV and load in watts.
- `battery_power`: site battery watts, positive charging, negative discharging.
- `grid_power`: site grid watts, positive import, negative export.
- `battery_perc`: arithmetic mean of the two battery SOC reports, percent.
  Null until both battery reports exist. No single-battery substitution.
- `stale`: true when Wi-Fi is disconnected, any required report is missing,
  or the site/either battery report is at least 120 seconds old (monotonic age).

Missing values are JSON null. Expired values remain available with stale=true.
Reboot clears the cache. A new report is required to populate it again.
The display retains its existing independent 10-minute freshness policy.

Actual source: MikroTik mirrors SDongle traffic to UDP/TZSP port 37008 on
the Pico. The existing decoder accepts new source timestamps only. No HTTP
poller or dependency on dark.lan:8734 / logger .154 is added. A 30-60 second
HTTP poller would require a confirmed URL, authentication and data contract;
polling this passive UDP source is not possible. Normal source reports can
be more than two minutes apart, so stale=true does not by itself prove failure.
WAN disconnection is not directly detected: freshness follows incoming reports.

Example (key supplied privately):
```sh
curl -s -H "Authorization: Bearer $K" http://192.168.1.172/api/readings
```

The existing HTTP server accepts one connection at a time. Avoid concurrent
pollers and allow a short gap between requests (for example 200 ms); this
existing restriction also applies to settings/OTA. Normal monitoring should
poll much less frequently than this. Run `python pico/check_readings_live.py`
for non-destructive authentication, latency and settings-preservation checks.

Run `pico/test_solar.ps1` for cache expiry, missing-data, numeric bounds and
existing decoder/display/OTA regression tests. Build using `pico/build_solar.ps1`.
Upload the signed package with the existing panel_admin.py tool. Do not run
destructive OTA rollback tests on an unattended installed panel.
