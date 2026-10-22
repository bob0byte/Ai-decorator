# For every level: one typical real decoration per (hitbox kind, open-side pattern), copied exactly.
import sys, os, json, pickle, collections
import numpy as np
from scipy.spatial import cKDTree
P = sys.argv[1]
exec(open(P + '/pack/scripts/analyze.py').read().split("def analyze")[0])
NPZ = P + '/pack/npz'
GR = {'block', 'platform', 'slope'}
KINDS = ['block', 'platform', 'slope', 'spike', 'ground_spike', 'saw']
out = {}
for fn in sorted(f for f in os.listdir(NPZ) if f.endswith('.npz')):
    meta = pickle.load(open(NPZ + '/' + fn.replace('.npz', '.meta.pkl'), 'rb'))
    try:
        a = np.load(NPZ + '/' + fn)['obj']
        ctl = ColorTimeline(palette(meta), meta['triggers'])
        ids = a[:, 0].astype(int); ids[(ids < 0) | (ids >= 5000)] = 0
        cats = np.array([CATS[c] for c in cat_of[ids]])
        gp = np.isin(cats, list(GR)); isdeco = np.isin(cats, ['deco', 'deco22'])
        cells = set(zip(np.floor(a[gp, 1] / 30).astype(int).tolist(), np.floor(a[gp, 2] / 30).astype(int).tolist()))
        ai = np.nonzero(np.isin(cats, KINDS + ['spike_slope', 'orb', 'pad', 'portal']))[0]; di = np.nonzero(isdeco)[0]
        if len(ai) < 10 or len(di) < 10: continue
        tree = cKDTree(a[ai, 1:3]); dist, nn = tree.query(a[di, 1:3], k=1, distance_upper_bound=32)
        owner = collections.defaultdict(list)
        for k in np.nonzero(np.isfinite(dist))[0]:
            p = int(di[k]); sc = max(abs(a[p, 7] * a[p, 6]), abs(a[p, 8] * a[p, 6]))
            if sc > 2.2: continue  # big art / background pieces are not block decoration
            owner[int(ai[nn[k]])].append(p)
        c1 = np.where(a[:, 11] > 0, a[:, 11], def_color[ids]).astype(int)
        groups = collections.defaultdict(list)
        for j in ai:
            kind = cats[j]
            if kind not in KINDS: continue
            if abs(a[j, 3]) % 90 > 1: continue  # only axis-aligned anchors
            pcs = owner.get(int(j), [])
            if len(pcs) < 2: continue
            cx, cy = int(np.floor(a[j, 1] / 30)), int(np.floor(a[j, 2] / 30))
            sig = ''.join(str(int((cx + dx, cy + dy) not in cells)) for dx, dy in ((0, 1), (0, -1), (-1, 0), (1, 0))) if kind in GR else 'x'
            groups[(kind, sig)].append((int(j), pcs))
        lv = {}
        for (kind, sig), lst in groups.items():
            if len(lst) < 3: continue
            ns = np.array([len(p) for _, p in lst]); med = np.median(ns)
            j, pcs = min(lst, key=lambda t: (abs(len(t[1]) - med), t[0]))  # a typical one
            pcs = pcs[:14]
            cols = ctl.colors(c1[pcs], a[pcs, 1])
            lv[f'{kind}|{sig}'] = [[int(ids[p]), round(float(a[p, 1] - a[j, 1]), 1), round(float(a[p, 2] - a[j, 2]), 1),
                                    round(float(a[p, 3] - a[j, 3]) % 360, 1), round(float(a[p, 7] * a[p, 6]), 2), round(float(a[p, 8] * a[p, 6]), 2),
                                    int(a[p, 9] or def_z[ids[p]]), int(a[p, 10]), int(a[p, 4] > 0), int(a[p, 5] > 0),
                                    int(cols[k][0]), int(cols[k][1]), int(cols[k][2]), int(cols[k][3])] for k, p in enumerate(pcs)]
        if lv: out[meta['name']] = lv
    except Exception as e:
        print('ERR', meta['name'], e)
json.dump(out, open(P + '/pack/results/12_exemplars.json', 'w'), separators=(',', ':'))
print('levels', len(out), 'entries', sum(len(v) for v in out.values()), 'bytes', os.path.getsize(P + '/pack/results/12_exemplars.json'))
print(sorted(out.get('Apocalyptic Trilogy', {}).keys()))
