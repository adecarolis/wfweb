"""Integration coverage for the server-side ADIF logbook REST API.

The log is never returned whole: GET pages newest-first with a cursor.  These
tests add a handful of QSOs out of order and check ordering, paging, the
callsign filter, edit/delete, the ADIF download, import and clear.
"""

from pathlib import Path

import requests


def _base(rest_url):
    return rest_url.removesuffix("/radio") + "/logbook"


def _qso(call, date, time, **extra):
    q = {"date": date, "time": time, "call": call, "freq": 14074000,
         "band": "20M", "mode": "FT8", "rstSent": "-08", "rstRcvd": "-11"}
    q.update(extra)
    return q


def _logbook_dir(rest_url):
    """The server tells us where the file is (the data directory differs per
    platform: XDG on Linux, ~/Library/Application Support on macOS)."""
    info = requests.get(rest_url + "/info", timeout=5).json()
    return Path(info.get("info", info)["logbookPath"]).parent


def _clear(base, rest_url=None):
    had = requests.get(base, timeout=5).json()["total"]
    logbook_dir = _logbook_dir(rest_url or base.replace("/logbook", "/radio"))
    before = set(logbook_dir.glob("logbook.adi.*.bak"))
    r = requests.delete(base, timeout=5)
    assert r.status_code == 202
    assert requests.get(base, timeout=5).json()["total"] == 0
    new_baks = set(logbook_dir.glob("logbook.adi.*.bak")) - before
    if had:
        # clearing never discards data: the previous file is kept as a .bak
        assert len(new_baks) == 1, new_baks
        assert new_baks.pop().read_text().count("<EOR>") == had
    else:
        assert not new_baks


def test_logbook_crud_and_adif_download(rest_url):
    base = _base(rest_url)
    _clear(base)

    created = requests.post(base, json=_qso("k1test", "20260917", "120000", theirGrid="fn42"), timeout=5)
    assert created.status_code == 202
    entry = created.json()
    assert entry["call"] == "K1TEST"          # normalised to upper case
    assert entry["theirGrid"] == "FN42"
    assert entry["id"]

    listed = requests.get(base, timeout=5).json()
    assert listed["total"] == 1
    assert [e["id"] for e in listed["entries"]] == [entry["id"]]
    assert "next" not in listed                # single page

    updated = requests.put(f"{base}/{entry['id']}", json=_qso("K1TEST", "20260917", "120000", rstRcvd="-09"), timeout=5)
    assert updated.status_code == 200
    assert updated.json()["rstRcvd"] == "-09"
    assert updated.json()["id"] == entry["id"]  # id survives an edit

    adif = requests.get(f"{base}/adif", timeout=5)
    assert adif.status_code == 200
    assert "attachment" in adif.headers.get("Content-Disposition", "")
    assert "<EOH>" in adif.text
    assert "<CALL:6>K1TEST" in adif.text
    assert "<RST_RCVD:3>-09" in adif.text
    assert f"<APP_WFWEB_ID:{len(entry['id'])}>{entry['id']}" in adif.text

    deleted = requests.delete(f"{base}/{entry['id']}", timeout=5)
    assert deleted.status_code == 202
    assert requests.get(f"{base}/{entry['id']}", timeout=5).status_code == 404
    assert requests.get(base, timeout=5).json()["total"] == 0


def test_logbook_rejects_bad_input(rest_url):
    base = _base(rest_url)
    assert requests.post(base, json={"date": "20260917"}, timeout=5).status_code == 400
    assert requests.post(base, json={"call": "X" * 40}, timeout=5).status_code == 400
    assert requests.put(f"{base}/no-such-id", json=_qso("K1A", "20260917", "120000"), timeout=5).status_code == 404
    assert requests.delete(f"{base}/no-such-id", timeout=5).status_code == 404
    assert requests.patch(base, timeout=5).status_code == 405


def test_logbook_orders_pages_and_filters(rest_url):
    base = _base(rest_url)
    _clear(base)

    # Added out of chronological order on purpose.
    for call, date, time in [("N2MID", "20260102", "120000"),
                             ("N1OLD", "20260101", "090000"),
                             ("N4NEW", "20260103", "180000"),
                             ("N3", "20260103", "100000"),
                             ("N1OLD", "20260101", "230000")]:
        assert requests.post(base, json=_qso(call, date, time), timeout=5).status_code == 202

    # Newest first regardless of insertion order.
    full = requests.get(base, timeout=5).json()
    assert full["total"] == 5
    assert [e["call"] for e in full["entries"]] == ["N4NEW", "N3", "N2MID", "N1OLD", "N1OLD"]
    assert [e["time"] for e in full["entries"]][3:] == ["230000", "090000"]

    # Walk the log two at a time with the cursor.
    seen, cursor, pages = [], "", 0
    while True:
        page = requests.get(base, params={"limit": 2, **({"before": cursor} if cursor else {})}, timeout=5).json()
        seen += [e["id"] for e in page["entries"]]
        pages += 1
        cursor = page.get("next", "")
        if not cursor:
            break
    assert pages == 3
    assert seen == [e["id"] for e in full["entries"]]

    # Callsign filter, also paged.
    n1 = requests.get(base, params={"call": "n1old"}, timeout=5).json()
    assert [e["time"] for e in n1["entries"]] == ["230000", "090000"]
    assert n1["total"] == 5   # total is the whole log, not the match count
    assert requests.get(base, params={"call": "NOBODY"}, timeout=5).json()["entries"] == []

    # A cursor pointing at a since-deleted entry still resolves.
    first = requests.get(base, params={"limit": 1}, timeout=5).json()
    requests.delete(f"{base}/{first['entries'][0]['id']}", timeout=5)
    after = requests.get(base, params={"limit": 2, "before": first["next"]}, timeout=5).json()
    assert [e["call"] for e in after["entries"]] == ["N3", "N2MID"]

    _clear(base)


def test_station_callsign_is_server_state_and_stamps_qsos(rest_url):
    base = _base(rest_url)
    station = rest_url.removesuffix("/radio") + "/station"
    _clear(base)

    # One station callsign + grid for the whole server, normalised and persisted.
    r = requests.put(station, json={"callsign": " w2erc ", "grid": "fn20"}, timeout=5)
    assert r.status_code == 200 and r.json() == {"callsign": "W2ERC", "grid": "FN20"}
    assert requests.get(station, timeout=5).json() == {"callsign": "W2ERC", "grid": "FN20"}
    info = requests.get(rest_url + "/info", timeout=5).json()
    info = info.get("info", info)
    assert info["stationCallsign"] == "W2ERC" and info["stationGrid"] == "FN20"
    # An empty grid in an update leaves the stored one alone.
    assert requests.put(station, json={"callsign": "W2ERC", "grid": ""}, timeout=5).json()["grid"] == "FN20"
    # A field left out keeps what is stored: a grid alone used to clear the callsign.
    assert requests.put(station, json={"grid": "fn31"}, timeout=5).json() == {"callsign": "W2ERC", "grid": "FN31"}
    assert requests.put(station, json={}, timeout=5).json() == {"callsign": "W2ERC", "grid": "FN31"}
    assert requests.put(station, json={"callsign": "W2ERC", "grid": "FN20"}, timeout=5).json()["grid"] == "FN20"

    # A logged QSO is stamped with it; the ADIF carries STATION_CALLSIGN.
    e = requests.post(base, json=_qso("K1TEST", "20260921", "100000"), timeout=5).json()
    assert e["stationCall"] == "W2ERC"
    adif = requests.get(f"{base}/adif", timeout=5).text
    assert "<STATION_CALLSIGN:5>W2ERC" in adif
    new = requests.get(f"{base}/adif", params={"new": 1}, timeout=5).text
    assert "<STATION_CALLSIGN:5>W2ERC" in new       # standard field, so it survives the clean export

    # An explicit value is kept; an imported file without one is not invented.
    e2 = requests.post(base, json=_qso("K2TEST", "20260921", "110000", stationCall="kf0zjt"), timeout=5).json()
    assert e2["stationCall"] == "KF0ZJT"
    doc = b"<eoh>\n<call:5>IK3ZZ <qso_date:8>20200101 <time_on:6>120000 <mode:3>FT8 <eor>\n"
    requests.post(f"{base}/adif", data=doc, timeout=5)
    ik3 = requests.get(base, params={"call": "IK3ZZ"}, timeout=5).json()["entries"][0]
    assert "stationCall" not in ik3
    _clear(base)


def test_logbook_import_merges_and_dedupes(rest_url):
    base = _base(rest_url)
    _clear(base)
    assert requests.post(base, json=_qso("K1TEST", "20260917", "120000"), timeout=5).status_code == 202

    # Lower-case tags, CRLF, a UTF-8 name whose byte length differs from its
    # character count, one duplicate of the existing QSO, no APP_WFWEB_ID.
    adif = ("Some other logger\r\n<adif_ver:5>3.1.4 <eoh>\r\n"
            "<call:6>K1TEST <qso_date:8>20260917 <time_on:6>120000 <freq:9>14.074000 <mode:3>FT8 <eor>\r\n"
            "<call:5>IK1ZZ <qso_date:8>20200505 <time_on:4>1010 <freq:8>7.074000 <band:3>40M <mode:3>FT8 "
            "<name:5>José <gridsquare:4>JN45 <eor>\r\n").encode("utf-8")
    imported = requests.post(f"{base}/adif", data=adif, timeout=5)
    assert imported.status_code == 202
    assert imported.json() == {"added": 1, "skipped": 1, "total": 2, "unexported": 1}   # K1TEST is still new

    entries = requests.get(base, timeout=5).json()["entries"]
    assert [e["call"] for e in entries] == ["K1TEST", "IK1ZZ"]
    ik1 = entries[1]
    assert ik1["name"] == "José"
    assert ik1["freq"] == 7074000
    assert ik1["theirGrid"] == "JN45"
    assert ik1["id"]

    # Re-importing the same document adds nothing.
    again = requests.post(f"{base}/adif", data=adif, timeout=5).json()
    assert again["added"] == 0 and again["skipped"] == 2
    _clear(base)


def test_logbook_export_bookkeeping(rest_url):
    base = _base(rest_url)
    _clear(base)

    # Logged QSOs are "new" until a client confirms it saved them.
    a = requests.post(base, json=_qso("K1NEW", "20260918", "100000"), timeout=5).json()
    b = requests.post(base, json=_qso("K2NEW", "20260918", "110000"), timeout=5).json()
    assert requests.get(base, timeout=5).json()["unexported"] == 2
    new = requests.get(f"{base}/adif", params={"new": 1}, timeout=5).text
    assert new.count("<EOR>") == 2 and "APP_WFWEB" not in new   # plain ADIF for other services

    # The JSON export carries the same clean document plus the ids to confirm.
    exp = requests.get(f"{base}/export", timeout=5).json()
    assert exp["count"] == 2 and sorted(exp["ids"]) == sorted([a["id"], b["id"]])
    assert exp["adif"] == new

    # Confirm only one of them: the other stays new.
    r = requests.post(f"{base}/exported", json={"ids": [a["id"], "no-such-id"]}, timeout=5).json()
    assert r == {"marked": 1, "unexported": 1}
    new = requests.get(f"{base}/adif", params={"new": 1}, timeout=5).text
    assert new.count("<EOR>") == 1 and "K2NEW" in new
    assert requests.get(f"{base}/export", timeout=5).json()["ids"] == [b["id"]]
    full = requests.get(f"{base}/adif", timeout=5).text
    assert full.count("<APP_WFWEB_EXPORTED:16>") == 1

    # The stamp survives an edit made from the listed object.
    entry = next(e for e in requests.get(base, timeout=5).json()["entries"] if e["id"] == a["id"])
    assert entry["exported"]
    entry["rstRcvd"] = "-01"
    edited = requests.put(f"{base}/{a['id']}", json=entry, timeout=5).json()
    assert edited["exported"] == entry["exported"]
    assert requests.get(base, timeout=5).json()["unexported"] == 1

    # Marking twice is a no-op.
    assert requests.post(f"{base}/exported", json={"ids": [a["id"]]}, timeout=5).json()["marked"] == 0

    # An imported file counts as already exported... unless asked otherwise.
    doc = ("<eoh>\n<call:5>IK3ZZ <qso_date:8>20200101 <time_on:6>120000 <mode:3>FT8 <eor>\n"
           "<call:5>IK4ZZ <qso_date:8>20200102 <time_on:6>120000 <mode:3>FT8 <eor>\n").encode()
    imp = requests.post(f"{base}/adif", data=doc, timeout=5).json()
    assert imp["added"] == 2 and imp["unexported"] == 1          # only K2NEW is still new
    doc2 = b"<eoh>\n<call:5>IK5ZZ <qso_date:8>20200103 <time_on:6>120000 <mode:3>FT8 <eor>\n"
    imp2 = requests.post(f"{base}/adif", params={"new": 1}, data=doc2, timeout=5).json()
    assert imp2["added"] == 1 and imp2["unexported"] == 2
    _clear(base)


def test_logbook_worked_summary(rest_url):
    """/logbook/worked folds the whole log into call -> bands (the FT8 panel's
    "new one" / "new on this band" hints)."""
    base = _base(rest_url)
    _clear(base)
    for call, date, freq, band in [("iz1abc", "20260101", 14074000, "20M"),
                                   ("IZ1ABC", "20260102", 7074000, "40M"),
                                   ("IZ1ABC", "20260103", 14074000, "20M"),   # same band twice
                                   ("K1ABC", "20260104", 14074000, "20M")]:
        r = requests.post(base, json=_qso(call, date, "120000", freq=freq, band=band), timeout=5)
        assert r.status_code == 202
    worked = requests.get(base + "/worked", timeout=5).json()
    assert worked["total"] == 4
    assert sorted(worked["calls"]["IZ1ABC"]) == ["20M", "40M"]
    assert worked["calls"]["K1ABC"] == ["20M"]
    assert requests.post(base + "/worked", json={}, timeout=5).status_code == 405
    _clear(base)
    assert requests.get(base + "/worked", timeout=5).json() == {"calls": {}, "total": 0}
