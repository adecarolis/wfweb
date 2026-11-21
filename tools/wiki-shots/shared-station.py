#!/usr/bin/env python3
"""Regenerates the pictures of the wiki's Shared Station page.

    tools/wiki-shots/shared-station.py <output dir>

Starts a --shared wfweb on a mock radio (port 18090, its own data dir),
makes sample accounts, and runs shared-station.mjs, which drives browsers
into each state and crops the element in question.  Never touches a real
radio or the operator's data dir.
"""
import asyncio, json, os, re, shutil, subprocess, sys, tempfile, threading, time
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, 'tests'))
import requests, urllib3
urllib3.disable_warnings()
from mock_radio import MockIcomRadio

out = os.path.abspath(sys.argv[1]); os.makedirs(out, exist_ok=True)
home = tempfile.mkdtemp(prefix='wfweb-shots-')
# The rig files, so the page has the rig's bands (the RULES tab's band buttons).
shutil.copytree(os.path.join(REPO, 'rigs'), home + '/.local/share/wfweb/wfweb/rigs')
loop = asyncio.new_event_loop(); threading.Thread(target=loop.run_forever, daemon=True).start()
mock = MockIcomRadio(radio_addr=0x98); asyncio.run_coroutine_threadsafe(mock.start(), loop).result(10)
log = home + '/wfweb.log'
U = 'https://127.0.0.1:18090'
p = subprocess.Popen([REPO + '/wfweb', '-p', '18090', '--lan', '127.0.0.1', '--lan-control', str(mock.control_port),
                      '--lan-user', 'testuser', '--lan-pass', 'testpass', '--civ', '0x98', '--shared',
                      '--shared-dir', home + '/station', '-l', log, '--no-tui'],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                     env=dict(os.environ, HOME=home, XDG_CONFIG_HOME=home + '/.config', XDG_DATA_HOME=home + '/.local/share'))
rc = 1
try:
    for _ in range(100):
        try:
            if requests.get(U + '/api/v1/auth/me', verify=False, timeout=2).status_code == 200: break
        except Exception: pass
        time.sleep(0.3)
    node = lambda phase, state: subprocess.run(['node', HERE + '/shared-station.mjs', phase, U, out, json.dumps(state)], check=True)
    node('setup', {})
    code = re.findall(r'setup code ([A-Z2-9]{4}-[A-Z2-9]{4})', open(log, errors='replace').read())[-1]
    ck = lambda r: r.cookies.get('wfweb_session')
    PW = 'sample-password-2026'
    r = requests.post(U + '/api/v1/auth/setup', json={'code': code, 'username': 'clubadmin', 'password': PW, 'callsign': 'W1XYZ'}, verify=False)
    cookies = {'clubadmin': ck(r)}
    adm = {'wfweb_session': cookies['clubadmin']}
    for n, role, call in (('maria', 'operate', 'W1ABC'), ('tom', 'operate', 'K2DEF'), ('lee', 'control', 'N3GHI'), ('anna', 'listen', '')):
        requests.post(U + '/api/v1/shared/users', json={'username': n, 'password': PW, 'role': role, 'callsign': call},
                      cookies=adm, verify=False)
        cookies[n] = ck(requests.post(U + '/api/v1/auth/login', json={'username': n, 'password': PW}, verify=False))
    node('main', {'cookies': cookies, 'code': code})
    rc = 0
finally:
    p.terminate()
    try: p.wait(10)
    except Exception: p.kill()
    asyncio.run_coroutine_threadsafe(mock.stop(), loop) if hasattr(mock, 'stop') else None
    shutil.rmtree(home, ignore_errors=True)
sys.exit(rc)
