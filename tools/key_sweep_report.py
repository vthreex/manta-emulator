"""Summarize single-key connected runs (one emulator per raw IR byte) into a machine-readable classification.

usage: key_sweep_report.py SWEEP_DIR OUT.json OUT.tsv
Each run injected one raw byte at dispatch 1,300,000,000 (--ir-script) and watched the OSD show call 804C3684, the hide call 804C3574,
the ZUI key post 803FFE40 and the key chain read 8046A758. Classification is deliberately conservative: only digits are VERIFIED.
"""
import glob, json, sys
from pathlib import Path

sweep, out_json, out_tsv = sys.argv[1:4]
table = {}
for line in open(Path(__file__).parent.parent / 'docs' / 'ir-raw-to-app-key-table.tsv'):
    if line.startswith('#'):
        continue
    raw, app, target = line.rstrip('\n').split('\t')
    table[int(raw, 0)] = app
DIGITS = {0x45: 0, **{r: r for r in range(1, 10)}}
rows = []
for f in sorted(glob.glob(sweep + '/k*.json')):
    raw = int(Path(f).stem[1:], 16)
    x = json.load(open(f))
    w = x['watched_pc_hits']
    hits = lambda pc: [h for h in w.get(pc, {}).get('hits', []) if h['dispatch'] > 1.299e9]
    shows = [(h['dispatch'], int(h['a0'], 16)) for h in hits('0x804c3684')]
    keyhit = [(h['dispatch'], h['v1']) for h in hits('0x8046a758')]
    rows.append({'raw': '0x%02x' % raw, 'app_code_table': table.get(raw), 'app_code_observed': keyhit[0][1] if keyhit else None,
                 'osd_show_first_ids': [s[1] for s in shows[:3]], 'osd_show_delay_dispatches': (shows[0][0] - 1302065306) if shows else None,
                 'zui_key_posts': len(hits('0x803ffe40')), 'stop_pc': x.get('stop_pc'), 'stop_reason': x.get('stop_reason') or x.get('reason'),
                 'dispatches': x['instruction_dispatches'],
                 'classification': 'VERIFIED digit %d' % DIGITS[raw] if raw in DIGITS else 'unidentified'})
json.dump({'injected_dispatch': 1300000000, 'state': 'connected NO SIGNAL / OSD 2 state (top state 0x14, ActiveOsd 2), no key forwarded to the ZUI', 'rows': rows}, open(out_json, 'w'), indent=1)
with open(out_tsv, 'w') as t:
    t.write('raw\tapp_code\tfirst_osd_shown\tzui_posts\tstop\tclassification\n')
    for r in rows:
        t.write('%s\t%s\t%s\t%d\t%s\t%s\n' % (r['raw'], r['app_code_observed'], ','.join(map(str, r['osd_show_first_ids'])), r['zui_key_posts'], r['stop_reason'], r['classification']))
print(len(rows), 'runs')
