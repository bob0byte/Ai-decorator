# Simulate the mod: build test structures, run the network, place decoration, render PNG maps.
# Also renders a real section of a level of the same type for side-by-side comparison.
import sys, json, pickle, csv, math, os
import numpy as np
from PIL import Image, ImageDraw

P = sys.argv[1]; OUT = P + '/pack/ml/sim'; os.makedirs(OUT, exist_ok=True)
meta = json.load(open(P + '/pack/ml/model_meta.json'))
W = pickle.load(open(P + '/pack/ml/ckpt.pkl', 'rb'))['W']
ref = {int(r['id']): r for r in csv.DictReader(open(P + '/pack/objects_reference.csv'))}
HB, TYPES, R, TOK = meta['hb'], meta['types'], meta['R'], meta['tokens']
NB = (2 * R + 1) ** 2
CODE = {'block': 1, 'platform': 1, 'slope': 2, 'spike': 3, 'ground_spike': 3, 'spike_slope': 3, 'saw': 3}
SIDES = ['center', 'top', 'bottom', 'left', 'right']
SIDE_ANG = {'center': 0, 'top': 0, 'right': 90, 'bottom': 180, 'left': 270}

def fam(i): return ref[i]['family'].lower() if i in ref else ''
def role(i):
    f = fam(i)
    if 'gradient' in f or 'glow' in f: return 'glow'
    if 'persp' in f: return 'persp'
    if 'chain' in f: return 'chain'
    if any(k in f for k in ('lightsquare', 'lighttriangle', 'whiteblock')) or f == 'd_square' or f.startswith('square_'): return 'body'
    if any(k in f for k in ('outline', 'gridline', 'd_bar', 'smallbar', 'd_link')): return 'line'
    if any(k in f for k in ('ball', 'scalefadering', 'circle', 'spark')): return 'particle'
    return 'other'

def predict(X):
    h = np.maximum(X @ W[0] + W[1], 0); h = np.maximum(h @ W[2] + W[3], 0)
    return 1 / (1 + np.exp(-(h @ W[4] + W[5])))

def features(hb, side, q, t, nb):
    x = np.zeros(meta['nin'], np.float32); o = 0
    x[o + hb] = 1; o += len(HB); x[o + side] = 1; o += 5; x[o + q] = 1; o += 4; x[o + t] = 1; o += len(TYPES)
    for c, v in enumerate(nb):
        if v: x[o + c * 4 + v - 1] = 1
    return x

def decorate(struct, t, thr=0.3, k=2):
    occ = {}
    for cls, x, y, rot in struct:
        key = (math.floor(x / 30), math.floor(y / 30)); occ[key] = max(occ.get(key, 0), CODE.get(cls, 4))
    out = []
    for cls, x, y, rot in struct:
        cx, cy = math.floor(x / 30), math.floor(y / 30); q = int(round(rot / 90)) % 4
        nb = [0 if (dx == 0 and dy == 0) else occ.get((cx + dx, cy + dy), 0) for dy in range(-R, R + 1) for dx in range(-R, R + 1)]
        X = np.stack([features(HB.index(cls) if cls in HB else 7, s, q, t, nb) for s in range(5)])
        p = predict(X)
        gridded = cls in ('block', 'platform', 'slope')
        for s in range(5):
            side = SIDES[s]
            if gridded and side != 'center':
                d = {'top': (0, 1), 'bottom': (0, -1), 'left': (-1, 0), 'right': (1, 0)}[side]
                dx, dy = d
                for _ in range(q): dx, dy = dy, -dx
                if (cx + dx, cy + dy) in occ: continue
            order = np.argsort(-p[s])
            used_roles = set()
            for j in order[:12]:
                if p[s, j] < thr or len(used_roles) >= k: break
                did, along, outd, rr, sx, sy, z, zo = TOK[j]
                r_ = role(did)
                if r_ in used_roles or r_ == 'other': continue
                used_roles.add(r_)
                a = math.radians(SIDE_ANG[side]); th = math.radians(rot)
                lx = along * math.cos(a) + outd * math.sin(a); ly = -along * math.sin(a) + outd * math.cos(a)
                wx = x + lx * math.cos(th) + ly * math.sin(th); wy = y - lx * math.sin(th) + ly * math.cos(th)
                out.append((did, wx, wy, (rr + rot + SIDE_ANG[side]) % 360, sx, sy, z, zo, r_, float(p[s, j])))
    return out

# ---------------- test structures
def blocks(x0, y0, w, h): return [('block', x0 + 30 * i + 15, y0 + 30 * j + 15, 0) for i in range(w) for j in range(h)]
def structures():
    s = {}
    s['floor+spikes'] = blocks(0, 0, 14, 2) + [('spike', 30 * i + 15, 75, 0) for i in (4, 5, 9)]
    s['pillar+saw'] = blocks(0, 0, 10, 1) + blocks(120, 30, 2, 4) + [('saw', 330, 150, 0)]
    s['stairs'] = sum([blocks(30 * i, 0, 1, i + 1) for i in range(7)], [])
    s['ceiling+floor'] = blocks(0, 0, 12, 1) + blocks(0, 150, 12, 1) + [('spike', 30 * i + 15, 45, 0) for i in (3, 7)] + [('spike', 30 * i + 15, 135, 180) for i in (5, 9)]
    s['platforms'] = [('platform', 30 * i + 15, 60 + 30 * (i // 4 % 2), 0) for i in range(12)] + [('saw', 120, 150, 0)]
    return s

# ---------------- rendering
PAL = {'body': (8, 8, 14), 'glow': (60, 220, 120), 'line': (235, 240, 245), 'particle': (60, 220, 120), 'chain': (200, 200, 210), 'persp': (40, 40, 60), 'other': (150, 150, 150)}
def render(gameplay, deco, path, palette=PAL, scale=3, pad=90):
    xs = [g[1] for g in gameplay] + [d[1] for d in deco]; ys = [g[2] for g in gameplay] + [d[2] for d in deco]
    x0, x1, y0, y1 = min(xs) - pad, max(xs) + pad, min(ys) - pad, max(ys) + pad
    Wd, Hd = int((x1 - x0) * scale), int((y1 - y0) * scale)
    img = Image.new('RGB', (Wd, Hd), (12, 14, 24)); glow = Image.new('RGB', (Wd, Hd), (0, 0, 0))
    def tf(x, y): return ((x - x0) * scale, (y1 - y) * scale)
    def poly(pts, x, y, rot, sx, sy):
        th = math.radians(rot); out = []
        for px, py in pts:
            px *= sx; py *= sy
            out.append(tf(x + px * math.cos(th) + py * math.sin(th), y - px * math.sin(th) + py * math.cos(th)))
        return out
    sq = [(-15, -15), (15, -15), (15, 15), (-15, 15)]
    for d in sorted(deco, key=lambda d: (d[6], d[7])):
        did, x, y, rot, sx, sy, z, zo, r_ = d[:9]; col = palette.get(r_, PAL['other'])
        if r_ == 'glow':  # gradient: bright at the edge (bottom of sprite), fading up
            for k in range(10):
                f = 1 - k / 10
                pts = poly([(-15, -15 + 3 * k), (15, -15 + 3 * k), (15, -12 + 3 * k), (-15, -12 + 3 * k)], x, y, rot, sx, sy)
                ImageDraw.Draw(glow).polygon(pts, fill=tuple(int(c * f * .8) for c in col))
        elif r_ == 'particle':
            cx_, cy_ = tf(x, y); rr = 6 * scale * sx
            ImageDraw.Draw(img).ellipse([cx_ - rr, cy_ - rr, cx_ + rr, cy_ + rr], outline=col, width=2)
        elif r_ == 'line':
            ImageDraw.Draw(img).polygon(poly([(-15, -1.5), (15, -1.5), (15, 1.5), (-15, 1.5)], x, y, rot, sx, sy), fill=col)
        elif r_ == 'chain':
            ImageDraw.Draw(img).line(poly([(0, -15), (0, 15)], x, y, rot, sx, sy), fill=col, width=3)
        else:
            ImageDraw.Draw(img).polygon(poly(sq, x, y, rot, sx, sy), fill=col)
    img = Image.fromarray(np.minimum(255, np.asarray(img, np.int32) + np.asarray(glow, np.int32)).astype(np.uint8))
    dr = ImageDraw.Draw(img)
    for cls, x, y, rot in gameplay:
        if cls in ('block', 'platform'):
            h = 15 if cls == 'platform' else 30
            dr.polygon(poly([(-15, -15), (15, -15), (15, -15 + h), (-15, -15 + h)], x, y, rot, 1, 1), outline=(255, 255, 255))
        elif cls == 'spike':
            dr.polygon(poly([(-15, -15), (15, -15), (0, 15)], x, y, rot, 1, 1), outline=(255, 90, 90))
        elif cls == 'saw':
            cx_, cy_ = tf(x, y); rr = 22 * scale; dr.ellipse([cx_ - rr, cy_ - rr, cx_ + rr, cy_ + rr], outline=(255, 90, 90), width=2)
    img.save(path)

if __name__ == '__main__':
    which = sys.argv[2].split(',') if len(sys.argv) > 2 else TYPES
    thr = float(sys.argv[3]) if len(sys.argv) > 3 else 0.3
    for t in which:
        ti = TYPES.index(t)
        for name, st in structures().items():
            deco = decorate(st, ti, thr)
            render(st, deco, f'{OUT}/{t}_{name}.png')
            print(t, name, len(st), 'gameplay ->', len(deco), 'deco', sorted({(d[8]) for d in deco}))

# ---------------- real level sections for comparison
def real_section(type_name, width=420):
    exec_ns = {}
    src = open(P + '/pack/scripts/analyze.py').read().split("def palette")[0]
    old = sys.argv; sys.argv = ['x', P]; exec(src, exec_ns); sys.argv = old
    cat_of, CATS = exec_ns['cat_of'], exec_ns['CATS']
    types = json.load(open(P + '/pack/results/10_deco_types.json'))['types']
    lvl = types[type_name][0]
    npz = P + '/pack/npz'
    fn = [f for f in os.listdir(npz) if f.endswith('.meta.pkl') and pickle.load(open(npz + '/' + f, 'rb'))['name'] == lvl][0]
    a = np.load(npz + '/' + fn.replace('.meta.pkl', '.npz'))['obj']
    ids = a[:, 0].astype(int); ids[(ids < 0) | (ids >= 5000)] = 0
    cats = [CATS[c] for c in cat_of[ids]]
    gp = np.array([c in CODE for c in cats])
    xs = a[gp, 1]
    # densest gameplay window
    lo, hi = np.percentile(xs, [10, 90]); best = (0, lo)
    for s0 in np.arange(lo, hi - width, 60):
        n = ((xs >= s0) & (xs < s0 + width)).sum()
        if n > best[0]: best = (n, s0)
    s0 = best[1]
    m = (a[:, 1] >= s0) & (a[:, 1] < s0 + width)
    ys = a[m & gp, 2]; ylo, yhi = np.percentile(ys, [5, 95]) if len(ys) else (0, 300)
    m &= (a[:, 2] > ylo - 60) & (a[:, 2] < yhi + 60)
    gameplay = [(cats[i], a[i, 1], a[i, 2], a[i, 3]) for i in np.nonzero(m & gp)[0]]
    deco = []
    for i in np.nonzero(m & np.array([c in ('deco', 'deco22') for c in cats]))[0]:
        r_ = role(int(ids[i]))
        if r_ == 'other': continue
        deco.append((int(ids[i]), a[i, 1], a[i, 2], a[i, 3], a[i, 7] * a[i, 6], a[i, 8] * a[i, 6], a[i, 9], a[i, 10], r_, 1.0))
    return lvl, gameplay, deco

def compare(type_name, thr=0.3):
    lvl, g, d = real_section(type_name)
    render(g, d, f'{OUT}/REAL_{type_name}.png')
    ti = TYPES.index(type_name)
    d2 = decorate(g, ti, thr)
    render(g, d2, f'{OUT}/AI_{type_name}.png')
    return lvl, len(g), len(d), len(d2)
