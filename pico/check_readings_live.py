"""Read-only acceptance probe. Does not modify settings or interrupt telemetry."""
import json
import time
import urllib.error
import urllib.request
from panel_admin import Panel


def main():
    panel = Panel()
    before = panel.settings()
    for headers in ({}, {'Authorization': 'Bearer invalid'}):
        time.sleep(0.2)  # Existing HTTP server serves one connection at a time.
        request = urllib.request.Request(panel.base + '/api/readings', headers=headers)
        try:
            urllib.request.urlopen(request, timeout=5)
            raise AssertionError('Unauthenticated readings were accepted')
        except urllib.error.HTTPError as exc:
            assert exc.code == 401
            assert exc.read() == b'Invalid management key'
    durations = []
    for _ in range(5):
        time.sleep(0.2)
        start = time.monotonic()
        reading = panel.request('readings')
        durations.append(time.monotonic() - start)
        assert set(reading) == {'ts', 'stale', 'solar_power', 'battery_perc',
                                'battery_power', 'grid_power', 'consume_power'}
        assert type(reading['stale']) is bool
        for field, value in reading.items():
            if field != 'stale':
                assert value is None or type(value) in (int, float)
        time.sleep(0.2)
    assert max(durations) < 1, durations
    try:
        panel.request('readings', b'')
        raise AssertionError('POST readings accepted')
    except urllib.error.HTTPError as exc:
        assert exc.code == 409
    time.sleep(0.2)
    assert panel.settings() == before
    time.sleep(0.2)
    status = panel.request('status')
    assert status['version'] == '0.4.2-solar-readings'
    assert status['confirmed'] and status['backup'] and not status['updating']
    assert status['errors'] == 0
    print(json.dumps({'readings': reading, 'latency_ms': [round(t*1000, 1) for t in durations],
                      'status': status, 'auth_and_read_only': 'PASS'}))


if __name__ == '__main__':
    main()
