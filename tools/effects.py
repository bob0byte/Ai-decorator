# Per level: color-channel opacity used by glow / solid deco, and how Pulse triggers are used (spacing, timing, target).
import os, sys, csv, pickle, json, collections
import numpy as np
P = sys.argv[1]; NPZ = P + '/pack/npz'; R = P + '/pack/results'
ref = {int(r['id']): r for r in csv.DictReader(open(P + '/pack/objects_reference.csv'))}
isdeco = np.zeros(5000, bool)
for i, r in ref.items():
    if i < 5000 and r['category'] == 'deco': isdeco[i] = True
isdeco[1912:] = True
for i, r in ref.items():
    if i < 5000 and r['category'] != 'deco': isdeco[i] = False
def defcol(i): return int(ref[i]['color'] or 0) if i in ref else 0
trig = pickle.load(open(P + '/pack/triggers_all.pkl', 'rb'))
bylvl = collections.defaultdict(list)
for t in trig: bylvl[t['level']].append(t)
def f(v, d=0.0):
    try: return float(v)
    except Exception: return d
out = {}
for fn in sorted(x for x in os.listdir(NPZ) if x.endswith('.npz')):
    meta = pickle.load(open(NPZ + '/' + fn.replace('.npz', '.meta.pkl'), 'rb'))
    a = np.load(NPZ + '/' + fn)['obj']; ids = a[:, 0].astype(int); ids[(ids < 0) | (ids >= 5000)] = 0
    d = isdeco[ids]
    ch = np.where(a[:, 11] > 0, a[:, 11], [defcol(i) for i in ids]).astype(int)[d]
    cnt = collections.Counter(ch.tolist())
    # channel opacity and blending: header, else first non-spawn color trigger
    op, bl = {}, {}
    for c in meta['colors']:
        if isinstance(c.get('6'), int): op[c['6']] = f(c.get('7', 1), 1); bl[c['6']] = int(c.get('5', 0) or 0)
    for t in sorted(bylvl[meta['name']], key=lambda t: f(t.get('2'))):
        if t['1'] == 899 and t.get('62') not in (1, '1'):
            c = int(f(t.get('23', 1)))
            if c not in op or True:
                op.setdefault(c, f(t.get('35', 1), 1)); bl.setdefault(c, int(f(t.get('17', 0))))
    gl, so = [], []
    for c, n in cnt.items():
        o = op.get(c, 1.0); b = bl.get(c, 0)
        (gl if b else so).append((o, n))
    def wmed(lst):
        if not lst: return 1.0
        v = np.array([o for o, _ in lst]); w = np.array([n for _, n in lst], float); k = np.argsort(v); c = np.cumsum(w[k])
        return float(v[k][np.searchsorted(c, c[-1] / 2)])
    # pulses: non spawn/touch, sorted by x
    pul = [t for t in bylvl[meta['name']] if t['1'] == 1006 and t.get('62') not in (1, '1') and t.get('11') not in (1, '1')]
    pul.sort(key=lambda t: f(t.get('2')))
    tg = collections.defaultdict(list)
    for t in pul: tg[(int(f(t.get('52'))), int(f(t.get('51'))))].append(f(t.get('2')))
    gaps = []
    for k, xs in tg.items():
        if len(xs) > 3:
            g = np.diff(np.unique(np.round(xs))); g = g[(g >= 15) & (g <= 2000)]
            gaps.extend(g.tolist())
    gaps = np.array(gaps)
    tk = collections.Counter({k: len(v) for k, v in tg.items()})
    top_target = tk.most_common(1)[0][0] if tk else (0, 0)
    deco_channels = set(c for c, n in cnt.most_common(12))
    on_deco = sum(1 for t in pul if f(t.get('52')) == 0 and int(f(t.get('51'))) in deco_channels) / max(1, len(pul))
    white = sum(1 for t in pul if f(t.get('7')) > 200 and f(t.get('8')) > 200 and f(t.get('9')) > 200) / max(1, len(pul))
    out[meta['name']] = {
        'glow_opacity': round(wmed(gl), 3), 'solid_opacity': round(wmed(so), 3),
        'pulses': len(pul), 'pulse_gap': float(np.median(gaps)) if len(gaps) else 0.0,
        'fade_in': float(np.median([f(t.get('45')) for t in pul])) if pul else 0.0,
        'hold': float(np.median([f(t.get('46')) for t in pul])) if pul else 0.0,
        'fade_out': float(np.median([f(t.get('47')) for t in pul])) if pul else 0.0,
        'pulse_on_deco_channel': round(on_deco, 3), 'pulse_white': round(white, 3),
        'top_pulse_target_is_bg': top_target in ((0, 1000),), 'top_pulse_target_type': 'group' if top_target[0] == 1 else 'channel',
        'alpha_triggers': sum(1 for t in bylvl[meta['name']] if t['1'] == 1007),
        'move_triggers': sum(1 for t in bylvl[meta['name']] if t['1'] == 901),
    }
json.dump(out, open(R + '/11_level_effects.json', 'w'), indent=1)
v = list(out.values())
for k in ['glow_opacity', 'solid_opacity', 'pulse_gap', 'fade_in', 'hold', 'fade_out', 'pulse_on_deco_channel', 'pulse_white']:
    a = np.array([x[k] for x in v]); print(k, 'median', round(float(np.median(a)), 3), 'p25', round(float(np.percentile(a, 25)), 3), 'p75', round(float(np.percentile(a, 75)), 3))
