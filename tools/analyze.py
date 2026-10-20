# Per-level decoration analysis.
# For each level: classify objects, attach every deco object to its nearest gameplay "anchor",
# measure where/how it sits (side, exposed edge, canonical offset, rotation, scale, layer, color),
# and summarize. Writes one summary pickle per level.
import os, sys, csv, pickle, numpy as np, pandas as pd
from scipy.spatial import cKDTree

P = sys.argv[1]
NPZ = P + '/pack/npz'; OUT = P + '/pack/analysis'; os.makedirs(OUT, exist_ok=True)
ref = {int(r['id']): r for r in csv.DictReader(open(P + '/pack/objects_reference.csv'))}

CATS = ['deco', 'trigger', 'block', 'platform', 'slope', 'spike', 'ground_spike', 'spike_slope', 'saw',
        'orb', 'pad', 'portal', 'speed', 'collectible', 'special', 'deco22']
CI = {c: i for i, c in enumerate(CATS)}
cat_of = np.full(5000, CI['deco22'], np.int16)
def_color = np.zeros(5000, np.int16)
def_z = np.zeros(5000, np.int16)
for i, r in ref.items():
    if i < 5000:
        cat_of[i] = CI[r['category']]
        def_color[i] = int(r['color'] or 0)
        def_z[i] = int(r['z_layer'] or 0)
cat_of[0] = CI['special']
ANCHOR = [CI[c] for c in ['block', 'platform', 'slope', 'spike', 'ground_spike', 'spike_slope', 'saw', 'orb', 'pad', 'portal', 'speed', 'collectible']]
GRIDDED = [CI['block'], CI['platform'], CI['slope']]
DECO = [CI['deco'], CI['deco22']]
R_ATTACH = 45.0  # 1.5 blocks

def palette(meta):
    pal = {}
    for c in meta['colors']:
        ch = c.get('6')
        if not isinstance(ch, int): continue
        rgb = (int(c.get('1', 255) or 0), int(c.get('2', 255) or 0), int(c.get('3', 255) or 0))
        pal[ch] = {'rgb': rgb, 'blend': int(c.get('5', 0) or 0), 'opacity': float(c.get('7', 1) or 1),
                   'copy': int(c.get('9', 0) or 0), 'hsv': c.get('10'), 'player': int(c.get('4', 0) or 0) if isinstance(c.get('4', 0), (int, float)) else 0}
    # resolve copied colors (one or two hops)
    for _ in range(3):
        for ch, v in pal.items():
            if v['copy'] and v['copy'] in pal and v['copy'] != ch:
                v['rgb'] = pal[v['copy']]['rgb']
    return pal

def hsv_adjust(rgb, hsv):
    """Apply a GD HSV string 'h a s a v a sChecked a vChecked' to an (n,4) int array."""
    if not hsv or not isinstance(hsv, str): return rgb
    p = hsv.split('a')
    try: h, sm, vm = float(p[0]), float(p[1]), float(p[2]); sadd, vadd = p[3] == '1', p[4] == '1'
    except Exception: return rgb
    import colorsys
    out = rgb.copy()
    for i in range(len(rgb)):
        r, g, b = rgb[i, :3] / 255.0
        hh, ss, vv = colorsys.rgb_to_hsv(r, g, b)
        hh = (hh + h / 360.0) % 1.0
        ss = min(1, max(0, ss + sm if sadd else ss * sm)); vv = min(1, max(0, vv + vm if vadd else vv * vm))
        out[i, :3] = np.round(np.array(colorsys.hsv_to_rgb(hh, ss, vv)) * 255)
    return out

class ColorTimeline:
    """Channel colors along the level: header colors, then Color triggers (non spawn-triggered) by x."""
    def __init__(self, pal, triggers):
        self.base = {ch: v['rgb'] + (v['blend'],) for ch, v in pal.items()}
        self.copy = {ch: v['copy'] for ch, v in pal.items() if v['copy']}
        self.copy_hsv = {ch: v.get('hsv') for ch, v in pal.items()}
        ev = {}
        for t in triggers:
            if t.get('1') not in (899, 29, 30, 104, 105, 221, 717, 718, 743, 744, 900, 915): continue
            if t.get('62') in (1, '1'): continue
            ch = {29: 1000, 30: 1001, 104: 1002, 105: 1004, 221: 1, 717: 2, 718: 3, 743: 4, 744: 1003, 900: 1009, 915: 1005}.get(t.get('1'), t.get('23', 1))
            try: ch = int(ch)
            except Exception: continue
            rgb = (int(t.get('7', 255) or 0), int(t.get('8', 255) or 0), int(t.get('9', 255) or 0), int(t.get('17', 0) or 0))
            cp = (int(t.get('50', 0) or 0), t.get('49'))
            try: tx = float(t.get('2', 0))
            except Exception: tx = 0.0
            ev.setdefault(ch, []).append((tx, rgb, cp))
        self.ev = {ch: (np.array([e[0] for e in v]), [e[1] for e in v], [e[2] for e in v]) for ch, v in
                   ((ch, sorted(v, key=lambda e: e[0])) for ch, v in ev.items())}
    def at(self, ch, xs, depth=0):
        out = np.tile(np.array(self.base.get(ch, (255, 255, 255, 0)), np.int16), (len(xs), 1))
        if ch == 1010: out[:] = (0, 0, 0, 0)
        if ch in self.copy and depth < 3:
            out[:] = hsv_adjust(self.at(self.copy[ch], xs[:1], depth + 1), self.copy_hsv.get(ch))[0]
        if ch in self.ev:
            tx, rgbs, cps = self.ev[ch]
            k = np.searchsorted(tx, xs, side='right') - 1
            for j in np.unique(k):
                if j < 0: continue
                m = k == j
                if cps[j][0] and depth < 3 and cps[j][0] != ch:
                    out[m] = hsv_adjust(self.at(cps[j][0], xs[m][:1], depth + 1), cps[j][1])[0]
                else: out[m] = rgbs[j]
        return out
    def peak(self, ch, depth=0):
        """Brightest color this channel is ever given (resolving copies)."""
        cands = [np.array(self.base.get(ch, (255, 255, 255, 0)), np.int16)]
        if ch in self.copy and depth < 3:
            cands.append(hsv_adjust(self.peak(self.copy[ch], depth + 1)[None], self.copy_hsv.get(ch))[0])
        if ch in self.ev:
            tx, rgbs, cps = self.ev[ch]
            for j in range(len(rgbs)):
                if cps[j][0] and depth < 3 and cps[j][0] != ch:
                    cands.append(hsv_adjust(self.peak(cps[j][0], depth + 1)[None], cps[j][1])[0])
                else: cands.append(np.array(rgbs[j], np.int16))
        return max(cands, key=lambda c: int(c[:3].max()))
    def colors(self, chs, xs):
        out = np.zeros((len(chs), 4), np.int16)
        for ch in np.unique(chs):
            m = chs == ch
            c = self.at(int(ch), xs[m])
            # a blending glow that is black right now is invisible; use the color it flashes/fades to
            dark = (c[:, :3].max(1) < 12) & (c[:, 3] > 0)
            if dark.any(): c[dark] = self.peak(int(ch))
            out[m] = c
        return out

def analyze(fn):
    a = np.load(os.path.join(NPZ, fn))['obj']
    meta = pickle.load(open(os.path.join(NPZ, fn.replace('.npz', '.meta.pkl')), 'rb'))
    pal = palette(meta)
    ctl = ColorTimeline(pal, meta['triggers'])
    ids = a[:, 0].astype(np.int32); ids[(ids < 0) | (ids >= 5000)] = 0
    cat = cat_of[ids]
    x, y, rot = a[:, 1], a[:, 2], a[:, 3]
    fx, fy = a[:, 4] > 0, a[:, 5] > 0
    scale = a[:, 6]; sx = a[:, 7] * scale; sy = a[:, 8] * scale
    z = np.where(a[:, 9] != 0, a[:, 9], def_z[ids]).astype(np.int16)
    zo = a[:, 10]
    c1 = np.where(a[:, 11] > 0, a[:, 11], def_color[ids]).astype(np.int32)
    c2 = a[:, 12].astype(np.int32)
    groups = a[:, 13]; hue, sat, val = a[:, 14], a[:, 15], a[:, 16]
    notouch, noglow = a[:, 18] > 0, a[:, 19] > 0

    is_anchor = np.isin(cat, ANCHOR)
    is_deco = np.isin(cat, DECO)
    ai = np.nonzero(is_anchor)[0]; di = np.nonzero(is_deco)[0]
    res = {'name': meta['name'], 'file': fn, 'n': len(ids),
           'cat_counts': {CATS[k]: int(v) for k, v in zip(*np.unique(cat, return_counts=True))},
           'id_counts': dict(zip(*[t.tolist() for t in np.unique(ids, return_counts=True)])),
           'palette': pal}
    if len(ai) == 0 or len(di) == 0:
        pickle.dump(res, open(os.path.join(OUT, fn.replace('.npz', '.pkl')), 'wb')); return res

    # --- exposure grid for blocks/platforms/slopes
    g = np.isin(cat, GRIDDED)
    cells = set(zip(np.floor(x[g] / 30).astype(np.int64).tolist(), np.floor(y[g] / 30).astype(np.int64).tolist()))

    # --- attach deco to nearest anchor
    tree = cKDTree(np.c_[x[ai], y[ai]])
    dist, nn = tree.query(np.c_[x[di], y[di]], k=1, distance_upper_bound=R_ATTACH)
    att = np.isfinite(dist)
    d_att, anc = di[att], ai[nn[att]]
    d_free = di[~att]

    # local offsets in the anchor's frame (GD rotation is clockwise-positive)
    th = np.deg2rad(rot[anc])
    wx, wy = x[d_att] - x[anc], y[d_att] - y[anc]
    lx = wx * np.cos(th) - wy * np.sin(th)
    ly = wx * np.sin(th) + wy * np.cos(th)
    lx = np.where(fx[anc], -lx, lx); ly = np.where(fy[anc], -ly, ly)
    # side
    ax, ay = np.abs(lx), np.abs(ly)
    side = np.full(len(d_att), 'center', object)
    side[(ay >= ax) & (ly > 8)] = 'top'; side[(ay >= ax) & (ly < -8)] = 'bottom'
    side[(ax > ay) & (lx > 8)] = 'right'; side[(ax > ay) & (lx < -8)] = 'left'
    corner = (ax > 18) & (ay > 18)
    side[corner & (lx > 0) & (ly > 0)] = 'corner'; side[corner & (lx < 0) & (ly > 0)] = 'corner'
    side[corner & (ly < 0)] = 'corner_low'
    # canonical frame: rotate so the side faces "top"
    alpha = np.select([side == 'right', side == 'bottom', side == 'left'], [90.0, 180.0, 270.0], 0.0)
    ar = np.deg2rad(alpha)
    cxo = lx * np.cos(ar) - ly * np.sin(ar)
    cyo = lx * np.sin(ar) + ly * np.cos(ar)
    rel_rot = np.mod(rot[d_att] - rot[anc] - alpha + 360 * 4, 360)
    # exposed: neighbouring grid cell on that side is empty (only meaningful for gridded anchors)
    acx, acy = np.floor(x[anc] / 30).astype(np.int64), np.floor(y[anc] / 30).astype(np.int64)
    dirx = np.select([side == 'right', side == 'left'], [1, -1], 0)
    diry = np.select([side == 'top', side == 'bottom'], [1, -1], 0)
    # rotate the side direction back to world (anchors are usually 0/90/180/270)
    q = (np.round(rot[anc] / 90).astype(int) % 4)
    wdx = np.select([q == 0, q == 1, q == 2, q == 3], [dirx, diry, -dirx, -diry])
    wdy = np.select([q == 0, q == 1, q == 2, q == 3], [diry, -dirx, -diry, dirx])
    exposed = np.array([(cx_ + dx_, cy_ + dy_) not in cells for cx_, cy_, dx_, dy_ in
                        zip(acx.tolist(), acy.tolist(), wdx.tolist(), wdy.tolist())], bool)
    gridded_anchor = np.isin(cat[anc], GRIDDED)
    exposed = np.where(gridded_anchor & np.isin(side, ['top', 'bottom', 'left', 'right']), exposed, True)

    def rgb_of(ch):
        v = pal.get(int(ch))
        if v: return v['rgb'] + (v['blend'],)
        if ch == 1010: return (0, 0, 0, 0)
        if ch == 1011: return (255, 255, 255, 0)
        return (255, 255, 255, 0)
    cols = ctl.colors(c1[d_att], x[d_att])

    df = pd.DataFrame({
        'anchor_cat': cat[anc], 'anchor_id': ids[anc], 'side': side, 'exposed': exposed,
        'deco_id': ids[d_att], 'off_t': cxo, 'off_n': cyo, 'rel_rot': rel_rot,
        'sx': sx[d_att], 'sy': sy[d_att], 'flipx': fx[d_att], 'z': z[d_att], 'zo': zo[d_att],
        'ch': c1[d_att], 'ch2': c2[d_att], 'r': cols[:, 0], 'g': cols[:, 1], 'b': cols[:, 2], 'blend': cols[:, 3],
        'grouped': groups[d_att] > 0, 'hsv': (hue[d_att] != 0) | (sat[d_att] != 1) | (val[d_att] != 1),
        'noglow': noglow[d_att]})
    df['anchor_cat'] = df['anchor_cat'].map(lambda k: CATS[k])
    res['attached'] = summarize(df)
    # how many anchors / exposed sides exist (denominators for probabilities)
    an = pd.Series(cat[ai]).map(lambda k: CATS[k]).value_counts().to_dict()
    res['anchor_counts'] = an
    res['anchor_id_counts'] = pd.Series(ids[ai]).value_counts().to_dict()
    # exposed side counts for gridded anchors
    gi = ai[np.isin(cat[ai], GRIDDED)]
    gcx, gcy = np.floor(x[gi] / 30).astype(np.int64), np.floor(y[gi] / 30).astype(np.int64)
    exp = {}
    for name_, (dx_, dy_) in {'top': (0, 1), 'bottom': (0, -1), 'left': (-1, 0), 'right': (1, 0)}.items():
        exp[name_] = int(sum((cx_ + dx_, cy_ + dy_) not in cells for cx_, cy_ in zip(gcx.tolist(), gcy.tolist())))
    res['exposed_sides'] = exp
    res['n_attached'] = int(len(d_att)); res['n_free'] = int(len(d_free))

    # --- free-floating scenery (not near gameplay)
    if len(d_free):
        fcols = ctl.colors(c1[d_free], x[d_free])
        dist2, _ = tree.query(np.c_[x[d_free], y[d_free]], k=1)
        fdf = pd.DataFrame({'deco_id': ids[d_free], 'sx': sx[d_free], 'sy': sy[d_free], 'rot': np.mod(rot[d_free], 360),
                            'z': z[d_free], 'r': fcols[:, 0], 'g': fcols[:, 1], 'b': fcols[:, 2], 'blend': fcols[:, 3],
                            'dist': dist2, 'dy': y[d_free] - y[ai].mean(), 'grouped': groups[d_free] > 0})
        gb = fdf.groupby('deco_id')
        res['free'] = gb.agg(n=('sx', 'size'), sx=('sx', 'median'), sy=('sy', 'median'),
                             rot_mode=('rot', lambda s: s.round(-1).mode().iat[0]), z=('z', lambda s: s.mode().iat[0]),
                             r=('r', 'median'), g=('g', 'median'), b=('b', 'median'), blend=('blend', 'mean'),
                             dist=('dist', 'median'), grouped=('grouped', 'mean')).reset_index()
        res['free_dist_hist'] = np.histogram(np.minimum(dist2, 600), bins=[45, 90, 150, 240, 360, 480, 601])[0].tolist()
    # level extent, for densities
    res['extent'] = (float(x[ai].min()), float(x[ai].max()), float(y[ai].min()), float(y[ai].max()))
    allc = ctl.colors(c1[di], x[di])
    key = (allc[:, 0] // 16) * 4096 + (allc[:, 1] // 16) * 64 + (allc[:, 2] // 16) * 4 // 4
    res['deco_colors'] = pd.DataFrame({'r': allc[:, 0], 'g': allc[:, 1], 'b': allc[:, 2], 'blend': allc[:, 3]}).value_counts().head(40).to_dict()
    mid = np.array([x[ai].mean()])
    res['bg'] = ctl.at(1000, mid)[0].tolist(); res['ground'] = ctl.at(1001, mid)[0].tolist()
    res['bg_start'] = ctl.at(1000, np.array([0.0]))[0].tolist()
    # per-object-wide flags
    res['flags'] = {'grouped_deco': float((groups[di] > 0).mean()), 'hsv_deco': float(((hue[di] != 0) | (sat[di] != 1) | (val[di] != 1)).mean()),
                    'noglow': float(noglow[di].mean()), 'notouch_anchor': float(notouch[ai].mean()),
                    'z_hist': pd.Series(z[di]).value_counts().to_dict()}
    pickle.dump(res, open(os.path.join(OUT, fn.replace('.npz', '.pkl')), 'wb'))
    return res

def summarize(df):
    df['rot_b'] = (np.round(df['rel_rot'] / 15) * 15) % 360
    keys = ['anchor_cat', 'side', 'exposed', 'deco_id']
    gb = df.groupby(keys)
    s = gb.agg(n=('off_n', 'size'), off_t=('off_t', 'median'), off_t_sd=('off_t', 'std'), off_n=('off_n', 'median'),
               off_n_sd=('off_n', 'std'), rot=('rot_b', lambda v: v.mode().iat[0]), rot_share=('rot_b', lambda v: (v == v.mode().iat[0]).mean()),
               sx=('sx', 'median'), sy=('sy', 'median'), flipx=('flipx', 'mean'), z=('z', lambda v: v.mode().iat[0]),
               zo=('zo', 'median'), r=('r', 'median'), g=('g', 'median'), b=('b', 'median'), blend=('blend', 'mean'),
               grouped=('grouped', 'mean'), hsv=('hsv', 'mean'), noglow=('noglow', 'mean'),
               anchor_ids=('anchor_id', lambda v: v.value_counts().head(3).to_dict())).reset_index()
    return s

if __name__ == '__main__':
    files = sorted(f for f in os.listdir(NPZ) if f.endswith('.npz'))
    part, nparts = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (0, 1)
    for k, fn in enumerate(files):
        if k % nparts != part: continue
        if os.path.exists(os.path.join(OUT, fn.replace('.npz', '.pkl'))): continue
        try:
            r = analyze(fn)
            print(r['name'], r['n'], r.get('n_attached'), r.get('n_free'), flush=True)
        except Exception as e:
            import traceback; print('ERROR', fn, e, traceback.format_exc()[-400:], flush=True)
