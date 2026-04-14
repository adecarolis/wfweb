"""Test error handling and edge cases in the REST API."""

import requests


def test_unknown_path_returns_404(rest_url):
    """GET on an unknown API path should return 404."""
    r = requests.get(f"{rest_url}/nonexistent", timeout=5)
    assert r.status_code == 404


def test_wrong_method_returns_405(rest_url):
    """POST on a GET-only endpoint should return 405."""
    r = requests.post(f"{rest_url}/frequency", timeout=5)
    assert r.status_code == 405


def test_delete_on_get_endpoint_returns_405(rest_url):
    """DELETE on /frequency should return 405."""
    r = requests.delete(f"{rest_url}/frequency", timeout=5)
    assert r.status_code == 405


def test_put_malformed_json(rest_url):
    """PUT with invalid JSON body should return 400."""
    r = requests.put(
        f"{rest_url}/frequency",
        data="not json",
        headers={"Content-Type": "application/json"},
        timeout=5,
    )
    assert r.status_code == 400


def test_put_frequency_missing_hz(rest_url):
    """PUT /frequency without 'hz' field should return 400."""
    r = requests.put(f"{rest_url}/frequency", json={"wrong": 123}, timeout=5)
    assert r.status_code == 400


def test_put_mode_missing_mode(rest_url):
    """PUT /mode without 'mode' field should return 400."""
    r = requests.put(f"{rest_url}/mode", json={"filter": 1}, timeout=5)
    assert r.status_code == 400


def test_cors_preflight(rest_url):
    """OPTIONS request should return CORS headers."""
    r = requests.options(
        f"{rest_url}/frequency",
        headers={
            "Origin": "http://example.com",
            "Access-Control-Request-Method": "PUT",
        },
        timeout=5,
    )
    # Should succeed (200 or 204)
    assert r.status_code in (200, 204)
    assert "access-control-allow-origin" in {k.lower(): v for k, v in r.headers.items()}


def test_get_meters_is_read_only(rest_url):
    """PUT on /meters (read-only endpoint) should return 405."""
    r = requests.put(f"{rest_url}/meters", json={"sMeter": 100}, timeout=5)
    assert r.status_code == 405
