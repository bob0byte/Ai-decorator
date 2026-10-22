# Build training data for the decoration network.
# One sample = one side (center/top/bottom/left/right) of one gameplay hitbox.
# Input  = hitbox class, side, rotation quarter, 7x7 neighbourhood of hitbox kinds around it, level deco type.
# Output = which decoration "tokens" real creators put on that side.
# A token = (deco id, relative rotation rounded to 45 deg, distance out from the side rounded to 5 units).
import os, sys, json, pickle, collections
import numpy as np
from scipy.spatial import cKDTree

P = sys.argv[1]
exec(open(P + '/pack/scripts/analyze.py').read().split("def palette")[0])  # CATS, cat_of, ANCHOR, GRIDDED, DECO, ...
NPZ = P + '/pack/npz'; OUT = P + '/pack/ml'; os.makedirs(OUT, exist_ok=True)
types = json.load(open(P + '/pack/results/10_deco_types.json'))['types']
TYPE_NAMES = sorted(types)
level_type = {n: TYPE_NAMES.index(t) for t, ns in types.items() for n in ns}

HB = ['block', 'platform', 'slope', 'spike', 'ground_spike', 'spike_slope', 'saw', 'other']
def hb_of(c):
    n = CATS[c]
    return HB.index(n) if n in HB else 7
SIDES = ['center', 'top', 'bottom', 'left', 'right']
SIDE_ANG = {'center': 0, 'top': 0, 'right': 90, 'bottom': 180, 'left': 270}
# neighbourhood cell codes: 0 empty, 1 solid (block/platform), 2 slope, 3 hazard, 4 other gameplay
CODE = {'block': 1, 'platform': 1, 'slope': 2, 'spike': 3, 'ground_spike': 3, 'spike_slope': 3, 'saw': 3}
R = 3  # 7x7
MAX_ANCHORS = int(sys.argv[2]) if len(sys.argv) > 2 else 2500
rng = np.random.default_rng(0)

samples = []  # (feat ints, token list)
token_count = collections.Counter()
token_stats = collections.defaultdict(list)

files = sorted(f for f in os.listdir(NPZ) if f.endswith('.npz'))
for fi, fn in enumerate(files):
    meta = pickle.load(open(os.path.join(NPZ, fn.replace('.npz', '.meta.pkl')), 'rb'))
    if meta['name'] not in level_type: continue
    lt = level_type[meta['name']]
    a = np.load(os.path.join(NPZ, fn))['obj']
    ids = a[:, 0].astype(np.int32); ids[(ids < 0) | (ids >= 5000)] = 0
    cat = cat_of[ids]
    x, y, rot = a[:, 1], a[:, 2], a[:, 3]
    sx, sy = a[:, 7] * a[:, 6], a[:, 8] * a[:, 6]
    z = np.where(a[:, 9] != 0, a[:, 9], def_z[ids]); zo = a[:, 10]
    fx, fy = a[:, 4] > 0, a[:, 5] > 0
    ai = np.nonzero(np.isin(cat, ANCHOR))[0]; di = np.nonzero(np.isin(cat, DECO))[0]
    if len(ai) < 20 or len(di) < 20: continue
    # occupancy grid
    occ = {}
    for j in ai:
        code = CODE.get(CATS[cat[j]], 4)
        occ[(int(np.floor(x[j] / 30)), int(np.floor(y[j] / 30)))] = max(code, occ.get((int(np.floor(x[j] / 30)), int(np.floor(y[j] / 30))), 0))
    # attach deco to nearest anchor (same as analyze.py)
    tree = cKDTree(np.c_[x[ai], y[ai]])
    dist, nn = tree.query(np.c_[x[di], y[di]], k=1, distance_upper_bound=45.0)
    ok = np.isfinite(dist)
    d_att, anc = di[ok], ai[nn[ok]]
    th = np.deg2rad(rot[anc])
    wx, wy = x[d_att] - x[anc], y[d_att] - y[anc]
    lx = wx * np.cos(th) - wy * np.sin(th); ly = wx * np.sin(th) + wy * np.cos(th)
    lx = np.where(fx[anc], -lx, lx); ly = np.where(fy[anc], -ly, ly)
    ax_, ay_ = np.abs(lx), np.abs(ly)
    side = np.full(len(d_att), 0)
    side[(ay_ >= ax_) & (ly > 8)] = 1; side[(ay_ >= ax_) & (ly < -8)] = 2
    side[(ax_ > ay_) & (lx < -8)] = 3; side[(ax_ > ay_) & (lx > 8)] = 4
    alpha = np.array([SIDE_ANG[SIDES[s]] for s in side], float); ar = np.deg2rad(alpha)
    cxo = lx * np.cos(ar) - ly * np.sin(ar); cyo = lx * np.sin(ar) + ly * np.cos(ar)
    rel = np.mod(rot[d_att] - rot[anc] - alpha, 360)
    rb = (np.round(rel / 45) * 45 % 360).astype(int)
    ob = np.clip(np.round(cyo / 5) * 5, -10, 40).astype(int)
    # group attachments by anchor index
    by_anchor = collections.defaultdict(list)
    for k in range(len(d_att)):
        by_anchor[int(anc[k])].append(k)
    chosen = ai if len(ai) <= MAX_ANCHORS else rng.choice(ai, MAX_ANCHORS, replace=False)
    for j in chosen:
        hb = hb_of(cat[j])
        cx, cy = int(np.floor(x[j] / 30)), int(np.floor(y[j] / 30))
        q = int(round(rot[j] / 90)) % 4
        nb = np.zeros((2 * R + 1, 2 * R + 1), np.int8)
        for dx in range(-R, R + 1):
            for dy in range(-R, R + 1):
                if dx == 0 and dy == 0: continue
                nb[dy + R, dx + R] = occ.get((cx + dx, cy + dy), 0)
        att = by_anchor.get(int(j), [])
        per_side = collections.defaultdict(set)
        for k in att:
            did = int(ids[d_att[k]])
            tok = (did, int(rb[k]), int(ob[k]))
            per_side[int(side[k])].add(tok)
            token_count[tok] += 1
            if len(token_stats[tok]) < 400:
                token_stats[tok].append((float(cxo[k]), float(cyo[k]), float(rel[k]), float(sx[d_att[k]]), float(sy[d_att[k]]), int(z[d_att[k]]), float(zo[d_att[k]])))
        for s in range(5):
            samples.append((hb, s, q, lt, nb.tobytes(), tuple(per_side.get(s, ()))))
    if fi % 20 == 0: print(fi, meta['name'], len(samples), len(token_count), flush=True)

pickle.dump({'samples': samples, 'token_count': token_count, 'token_stats': dict(token_stats), 'types': TYPE_NAMES, 'hb': HB, 'R': R},
            open(OUT + '/train_raw.pkl', 'wb'))
print('samples', len(samples), 'tokens', len(token_count))
