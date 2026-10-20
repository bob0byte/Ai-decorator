# Full trigger pass: every trigger object with all its properties, per level.
# Output: triggers.parquet-like pickle (DataFrame) with columns: level, id, x, y, and each property key.
import os, sys, json, csv, pickle
import pandas as pd

P = sys.argv[1]
L = P + '/pack/levels'
ref = {int(r['id']): r for r in csv.DictReader(open(P + '/pack/objects_reference.csv'))}
TRIG = {i for i, r in ref.items() if r['category'] == 'trigger'}
# 2.2 IDs not in the reference that behave like triggers are caught by trigger-only keys
TRIGGER_KEYS = {'10', '51', '45', '46', '47', '62', '87', '23', '28', '29', '68', '75', '63'}

rows = []
for fn in sorted(os.listdir(L)):
    lv = json.load(open(os.path.join(L, fn)))
    n0 = len(rows)
    for o in lv['objects']:
        i = o.get('1')
        if not isinstance(i, int): continue
        if i in TRIG or (i > 1911 and i not in ref and len(TRIGGER_KEYS & o.keys()) >= 2):
            r = {'level': lv['name']}
            r.update({k: v for k, v in o.items() if k != '31'})
            rows.append(r)
    print(lv['name'], len(rows) - n0, flush=True)
pickle.dump(rows, open(P + '/pack/triggers_all.pkl', 'wb'))  # sparse list of dicts (a dense table would be huge)
print('total', len(rows))
