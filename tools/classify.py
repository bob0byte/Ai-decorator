# Sort every level into community decoration styles using measurable features.
import os, sys, csv, pickle, json, colorsys, collections
import numpy as np, pandas as pd
P = sys.argv[1]; A = P + '/pack/analysis'; NPZ = P + '/pack/npz'; R = P + '/pack/results'
ref = {int(r['id']): r for r in csv.DictReader(open(P + '/pack/objects_reference.csv'))}
fam = lambda i: (ref[i]['family'].lower() if i in ref else '')
cat = lambda i: (ref[i]['category'] if i in ref else ('deco' if i > 1911 else ''))
trig = pickle.load(open(P + '/pack/triggers_all.pkl', 'rb'))
tcount = collections.defaultdict(collections.Counter)
for t in trig: tcount[t['level']][t['1']] += 1
names = {pickle.load(open(NPZ + '/' + f, 'rb'))['name']: f.replace('.meta.pkl', '') for f in os.listdir(NPZ) if f.endswith('.meta.pkl')}
rows = []
for fn in sorted(os.listdir(A)):
    r = pickle.load(open(A + '/' + fn, 'rb'))
    if 'attached' not in r: continue
    ids = {int(k): v for k, v in r['id_counts'].items()}
    deco = {i: n for i, n in ids.items() if cat(i) == 'deco'}
    dt = sum(deco.values()) or 1
    def share(*keys): return sum(n for i, n in deco.items() if any(k in fam(i) for k in keys)) / dt
    anchors = sum(r['anchor_counts'].values()) or 1
    # block rotations (diamond structures = 45/60 degree blocks)
    a = np.load(NPZ + '/' + names[r['name']] + '.npz')['obj']
    bid = a[:, 0].astype(int)
    isb = np.array([cat(i) in ('block', 'platform', 'slope') for i in bid])
    rot = np.mod(a[isb, 3], 90)
    diag = float(((np.abs(rot - 45) < 3) | (np.abs(rot - 30) < 3) | (np.abs(rot - 60) < 3)).mean()) if isb.any() else 0
    tc = tcount[r['name']]; ntrig = sum(tc.values())
    att = r['attached']
    blk_center = att[(att.anchor_cat == 'block') & (att.side == 'center')]
    black_body = float(blk_center[(blk_center.r < 25) & (blk_center.g < 25) & (blk_center.b < 25) & (blk_center.blend < .5)]['n'].sum() / max(1, r['anchor_counts'].get('block', 1)))
    dc = r['deco_colors']; w = np.array(list(dc.values()), float); cs = np.array(list(dc.keys()), float)
    hsv = np.array([colorsys.rgb_to_hsv(*(c[:3] / 255)) for c in cs]); viv = (hsv[:, 1] > .4) & (hsv[:, 2] > .35)
    hue = np.zeros(12)
    for (h, s_, v), ww, vv in zip(hsv, w, viv):
        if vv: hue[int(h * 12) % 12] += ww
    hue /= max(1, w.sum())
    rows.append({
        'level': r['name'],
        'glow': share('gradient', 'glow'),
        'persp3d': share('persp'),
        'textured': share('block0', 'blockdesign', 'square_', 'block1'),
        'particles': share('small_ball', 'scalefadering', 'ball', 'circle', 'drops', 'spark'),
        'lines': share('gridline', 'line'),
        'chains': share('chain'),
        'pixel': share('pixel'),
        'flat_squares': share('lightsquare', 'whiteblock', 'whiteblock'),
        'distinct_deco': len([i for i, n in deco.items() if n > 20]),
        'deco_per_gameplay': dt / anchors,
        'triggers_per_gameplay': ntrig / anchors,
        'move_alpha_rotate': (tc[901] + tc[1007] + tc[1346] + tc[3006] + tc[3009] + tc[2067]) / anchors,
        'pulse_per_gameplay': tc[1006] / anchors,
        'diagonal_blocks': diag,
        'black_body': black_body,
        'red_share': hue[0] + hue[1] * .5, 'green_share': hue[3] + hue[4], 'vivid': float(w[viv].sum() / w.sum()),
        'dark': float(w[(cs[:, :3].max(1) < 30)].sum() / w.sum()),
        'saws_per_100': 100 * r['anchor_counts'].get('saw', 0) / anchors,
    })
F = pd.DataFrame(rows)
q = lambda c, p: F[c].quantile(p)
# Score each community style (higher = more typical). Thresholds are relative to the pack (percentiles).
def z(c): return (F[c] - F[c].mean()) / (F[c].std() + 1e-9)
S = pd.DataFrame({'level': F.level})
S['Glow'] = z('glow') * 1.4 + z('black_body') * .3 - z('textured') * .3
S['Modern'] = z('black_body') * .8 + z('move_alpha_rotate') * .6 + z('flat_squares') * .5 - z('textured') * .6 - z('glow') * .2
S['Nine Circles'] = z('diagonal_blocks') * 1.6 + z('pulse_per_gameplay') * .6 + z('saws_per_100') * .3
S['Effect'] = z('triggers_per_gameplay') * 1.2 + z('move_alpha_rotate') * .9
S['Art'] = z('distinct_deco') * 1.0 + z('deco_per_gameplay') * .9 + z('particles') * .2
S['Classic'] = z('textured') * 1.6 - z('glow') * .3
S['3D'] = z('persp3d') * 1.7
S['Hell'] = z('red_share') * 1.3 + z('dark') * .5 + z('chains') * .4
S['Retro'] = z('green_share') * 1.0 + z('pixel') * 1.0 + z('lines') * .5
S['Particles'] = z('particles') * 1.5
S['Minimal'] = -z('deco_per_gameplay') * 1.0 - z('distinct_deco') * .8
types = [c for c in S.columns if c != 'level']
F['type'] = S[types].idxmax(axis=1)
F['type_score'] = S[types].max(axis=1)
F['second'] = S[types].apply(lambda r: r.drop(r.idxmax()).idxmax(), axis=1)
F.to_csv(R + '/10_level_deco_types.csv', index=False)
out = {}
for t in types:
    sub = F[F.type == t].sort_values('type_score', ascending=False)
    out[t] = sub.level.tolist()
    print(t, len(sub), sub.level.tolist()[:8])
json.dump({'types': out, 'scores': S.set_index('level').round(3).to_dict('index')}, open(R + '/10_deco_types.json', 'w'), indent=1)
