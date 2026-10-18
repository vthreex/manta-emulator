"""Map the idle AVD loop's input dependency frontier with the isolated probe.

Each round runs the unmodified original AVD program in `avd_mcu_probe`
and, when it stops on an unknown XDATA decision, fixes that one register to a
synthetic value (default 0, full mask) and reruns. The ordered list shows which
shared registers the loop needs and what the next missing one is. EVERY fixture
is synthetic: it explores control flow only, measures no hardware, and is never
available to the television executable.
"""
import argparse, json, re, subprocess, sys
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--probe', required=True)
p.add_argument('--avd', required=True)
p.add_argument('--bit7-11db', type=int, choices=(0, 1), default=0)
p.add_argument('--event-status', default='0')
p.add_argument('--rounds', type=int, default=40)
p.add_argument('--value', default='0')
p.add_argument('--report')
a = p.parse_args()

fixtures, rounds = [], []
for n in range(a.rounds):
    cmd = [a.probe, a.avd, '--deferred-shared-prefix', '--synthetic-11db-bit7', str(a.bit7_11db),
           '--synthetic-event-status', a.event_status, '--symbolic-code-tables', '--clear-only-event-ack', '--invalid-publication']
    for addr, val in fixtures:
        cmd += ['--synthetic-xdata', hex(addr), val, '0xff']
    out = subprocess.run(cmd, capture_output=True, text=True).stdout.splitlines()
    boundary = next((l for l in out if l.startswith('AVD_BOUNDARY')), '')
    count = next((l for l in out if l.startswith('AVD_ORIGINAL_INSTRUCTIONS')), '')
    last_read = None
    for l in out:
        m = re.search(r'avd_shared_register_read address 0x[0-9a-f]+ value 0x[0-9a-f]+ original AVD MOVX XDATA=(\d+)', l)
        if m:
            last_read = int(m.group(1))
    rounds.append({'fixtures': len(fixtures), 'boundary': boundary, 'run': count, 'last_shared_read': hex(last_read) if last_read else None})
    print(n, count.split('PC')[0].strip(), 'PC' + count.split('PC')[1] if 'PC' in count else '', '|', boundary[:90], '| last read', hex(last_read) if last_read else None)
    if 'unknown XDATA bits affect' not in boundary or last_read is None or any(f[0] == last_read for f in fixtures):
        break
    fixtures.append((last_read, a.value))
report = {'11db_bit7': a.bit7_11db, 'event_status': a.event_status, 'synthetic_fixture_order': [hex(f[0]) for f in fixtures],
          'rounds': rounds, 'note': 'synthetic exploration only; no measured hardware; not television input'}
if a.report:
    Path(a.report).write_text(json.dumps(report, indent=2) + '\n')
