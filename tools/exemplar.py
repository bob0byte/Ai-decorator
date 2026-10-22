# Example-based decoration: copy the exact decoration a real level put around a hitbox with the same surroundings.
import sys, os, json, pickle, math, collections
import numpy as np
from scipy.spatial import cKDTree
P = sys.argv[1]
src = open(P + '/pack/scripts/analyze.py').read()
exec(src.split("def analyze")[0])  # cat_of, CATS, ColorTimeline, palette, ...
NPZ = P + '/pack/npz'
GR = {'block', 'platform', 'slope'}

def load(level):
    fn = [f for f in os.listdir(NPZ) if f.endswith('.meta.pkl') and pickle.load(open(NPZ + '/' + f, 'rb'))['name'] == level][0]
    meta = pickle.load(open(NPZ + '/' + fn, 'rb'))
    a = np.load(NPZ + '/' + fn.replace('.meta.pkl', '.npz'))['obj']
    return meta, a

def exemplars(level):
    """For every block in the level: its open-side signature and every deco piece around it (relative)."""
    meta, a = load(level)
    ctl = ColorTimeline(palette(meta), meta['triggers'])
    ids = a[:, 0].astype(int); ids[(ids < 0) | (ids >= 5000)] = 0
    cats = np.array([CATS[c] for c in cat_of[ids]])
    gp = np.isin(cats, list(GR)); isdeco = np.isin(cats, ['deco', 'deco22'])
    bi = np.nonzero(cats == 'block')[0]; di = np.nonzero(isdeco)[0]
    cells = set(zip(np.floor(a[gp, 1] / 30).astype(int).tolist(), np.floor(a[gp, 2] / 30).astype(int).tolist()))
    ai = np.nonzero(np.isin(cats, list(GR) + ['spike', 'ground_spike', 'spike_slope', 'saw', 'orb', 'pad', 'portal']))[0]
    tree = cKDTree(a[ai, 1:3]); dist, nn = tree.query(a[di, 1:3], k=1, distance_upper_bound=40)
    owner = collections.defaultdict(list)
    for k in np.nonzero(np.isfinite(dist))[0]: owner[int(ai[nn[k]])].append(int(di[k]))
    c1 = np.where(a[:, 11] > 0, a[:, 11], def_color[ids]).astype(int)
    out = []
    for j in bi:
        cx, cy = int(np.floor(a[j, 1] / 30)), int(np.floor(a[j, 2] / 30))
        sig = tuple(int((cx + dx, cy + dy) not in cells) for dx, dy in ((0, 1), (0, -1), (-1, 0), (1, 0)))
        pieces = owner.get(int(j), [])
        if not pieces: continue
        cols = ctl.colors(c1[pieces], a[pieces, 1])
        rel = [(int(ids[p]), float(a[p, 1] - a[j, 1]), float(a[p, 2] - a[j, 2]), float(a[p, 3]), float(a[p, 7] * a[p, 6]), float(a[p, 8] * a[p, 6]),
                int(a[p, 9] or def_z[ids[p]]), int(a[p, 10]), bool(a[p, 4]), bool(a[p, 5]), tuple(int(v) for v in cols[k])) for k, p in enumerate(pieces)]
        out.append({'sig': sig, 'n': len(rel), 'pieces': rel, 'x': float(a[j, 1]), 'y': float(a[j, 2])})
    return out

if __name__ == '__main__':
    level = sys.argv[2]
    ex = exemplars(level)
    c = collections.Counter(e['sig'] for e in ex)
    print(level, 'decorated blocks', len(ex), 'signatures', c.most_common(8))
    iso = [e for e in ex if e['sig'] == (1, 1, 1, 1)]
    print('isolated blocks with deco', len(iso), 'pieces per block (median)', np.median([e['n'] for e in iso]) if iso else 0)
    pickle.dump(ex, open(P + '/pack/ex/' + level.replace(' ', '_') + '.pkl', 'wb'))
