# Lists AOT-compiled Java classes (<package>_<Class>_mt method-table symbols) in an ADB main.elf. Usage: aot_class_survey.py main.elf out.json
import re, collections, sys, json
d = open(sys.argv[1], 'rb').read()
# AOT class markers: <package_path_with_underscores>_<Class>_mt
mt = set(m.group(1).decode() for m in re.finditer(rb'\b((?:com|org|tw|javax|java|sun)_[A-Za-z0-9_]{3,150})_mt\x00', d))
print('class method tables', len(mt))
pk = collections.Counter()
for n in mt:
    parts = n.split('_')
    pk['_'.join(parts[:4])] += 1
for k, v in sorted(pk.items(), key=lambda x: -x[1])[:60]:
    print(v, k)
json.dump(sorted(mt), open(sys.argv[2], 'w'))
ui = sorted(n for n in mt if n.startswith('com_adb_xion_ui') or 'Dialog' in n)
print('ui/dialog-ish', len(ui))
for n in ui[:80]: print(' ', n)
