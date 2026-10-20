# Build style profiles for the mod from the aggregated analysis.
# Output: results/styles_for_mod.json (compact) + results/08_styles_report.md
import sys, json, pickle, colorsys, collections, csv
import numpy as np, pandas as pd
from sklearn.cluster import AgglomerativeClustering, KMeans
from sklearn.preprocessing import StandardScaler
from sklearn.decomposition import TruncatedSVD

P = sys.argv[1]; R = P + '/pack/results'
d = pickle.load(open(R + '/agg.pkl', 'rb'))
att, free, levels, F, X = d['att'], d['free'], d['levels'], d['F'], d['X_ids']
ref = {int(r['id']): r for r in csv.DictReader(open(P + '/pack/objects_reference.csv'))}
def fam(i): return ref[i]['family'] if i in ref else '2.2 object'
GRIDDED = {'block', 'platform', 'slope'}
SIDES = ['center', 'top', 'bottom', 'left', 'right']
ANCHOR_CATS = ['block', 'platform', 'slope', 'spike', 'ground_spike', 'spike_slope', 'saw', 'orb', 'pad', 'portal', 'speed', 'collectible']

# ---------------------------------------------------------------- style groups (Ward, 12)
svd = TruncatedSVD(n_components=12, random_state=0).fit_transform(np.sqrt(X))
num = F.drop(columns=['level', 'objects', 'style']).values
Z = np.hstack([StandardScaler().fit_transform(num), StandardScaler().fit_transform(svd) * 0.8])
F['style'] = AgglomerativeClustering(n_clusters=12, linkage='ward').fit(Z).labels_
lvl_by_name = {r['name']: r for r in levels}

HUES = ['Red', 'Orange', 'Yellow', 'Lime', 'Green', 'Teal', 'Cyan', 'Azure', 'Blue', 'Purple', 'Magenta', 'Pink']
FAMILY_WORDS = [('chain', 'Chains'), ('persp', '3D'), ('gridline', 'Grid'), ('small_ball', 'Particles'), ('ball', 'Orbs'),
                ('scalefadering', 'Rings'), ('gradient', 'Glow'), ('drops', 'Drips'), ('pixel', 'Pixel'), ('cloud', 'Clouds'),
                ('block0', 'Textured'), ('blockdesign', 'Textured'), ('outline', 'Outlines'), ('spike', 'Spiky'), ('circle', 'Circles')]

def palette_for(names, k=6):
    cols, w = [], []
    for n in names:
        for c, cnt in lvl_by_name[n]['deco_colors'].items():
            cols.append([c[0], c[1], c[2], 255 * (c[3] > 0)]); w.append(cnt)
    cols, w = np.array(cols, float), np.array(w, float)
    k = min(k, len(np.unique(cols, axis=0)))
    km = KMeans(n_clusters=k, n_init=10, random_state=0).fit(cols, sample_weight=w)
    pal = []
    for j in range(k):
        m = km.labels_ == j
        c = np.average(cols[m], axis=0, weights=w[m])
        pal.append({'rgb': [int(round(v)) for v in c[:3]], 'blend': int(c[3] > 127), 'weight': float(w[m].sum() / w.sum())})
    order = np.argsort([-p['weight'] for p in pal])
    return [pal[i] for i in order]

def nearest(pal, r, g, b, blend):
    best, bi = 1e9, 0
    for i, p in enumerate(pal):
        dd = (p['rgb'][0] - r) ** 2 + (p['rgb'][1] - g) ** 2 + (p['rgb'][2] - b) ** 2 + (0 if (p['blend'] > 0) == (blend > .5) else 9000)
        if dd < best: best, bi = dd, i
    return bi

def wmed(v, w):
    v, w = np.asarray(v, float), np.asarray(w, float); o = np.argsort(v); v, w = v[o], w[o]; c = np.cumsum(w)
    return float(v[np.searchsorted(c, c[-1] / 2)])
def wmode(v, w):
    return float(pd.Series(np.asarray(w)).groupby(np.asarray(v)).sum().idxmax())

def profile(names, top_items=12, min_share=0.01):
    names = set(names)
    lv = [lvl_by_name[n] for n in names]
    pal = palette_for(names)
    a = att[att['level'].isin(names) & att['side'].isin(SIDES)]
    anchors = collections.Counter(); exposed = collections.Counter(); gridded_total = 0
    for r in lv:
        anchors.update(r['anchor_counts'])
        for s, n in r['exposed_sides'].items(): exposed[s] += n
        gridded_total += sum(r['anchor_counts'].get(c, 0) for c in GRIDDED)
    slots = []
    for (ac, sd, ex), g in a.groupby(['anchor_cat', 'side', 'exposed']):
        if ac in GRIDDED and sd != 'center' and not ex: continue  # skip faces hidden against another block
        n_anchor = anchors.get(ac, 0)
        if n_anchor < 20: continue
        if ac in GRIDDED and sd != 'center':
            opp = exposed[sd] * n_anchor / max(1, gridded_total)
        else:
            opp = n_anchor
        total = g['n'].sum(); rate = total / max(1, opp)
        if rate < 0.02: continue
        items = []
        for did, h in g.groupby('deco_id'):
            w = h['n'].values; share = w.sum() / total
            if share < min_share: continue
            items.append([int(did), round(float(share), 4), round(wmed(h['off_t'], w), 1), round(wmed(h['off_n'], w), 1),
                          wmode(h['rot'], w), round(wmed(h['sx'], w), 2), round(wmed(h['sy'], w), 2),
                          round(float(np.average(h['flipx'], weights=w)), 2), int(wmode(h['z'], w)), int(round(wmed(h['zo'], w))),
                          nearest(pal, wmed(h['r'], w), wmed(h['g'], w), wmed(h['b'], w), float(np.average(h['blend'], weights=w)))])
        items.sort(key=lambda t: -t[1])
        if items:
            slots.append({'a': ac, 's': sd, 'rate': round(float(rate), 3), 'items': items[:top_items]})
    # scenery
    f = free[free['level'].isin(names)]
    tot_anchor = sum(anchors.values()) or 1
    scn = {'rate': round(float(f['n'].sum() / tot_anchor), 3), 'items': []}
    tf = f['n'].sum() or 1
    for did, h in f.groupby('deco_id'):
        w = h['n'].values
        if w.sum() / tf < 0.01: continue
        scn['items'].append([int(did), round(float(w.sum() / tf), 4), round(wmed(h['sx'], w), 2), round(wmed(h['sy'], w), 2),
                             wmode(h['rot_mode'], w), int(wmode(h['z'], w)),
                             nearest(pal, wmed(h['r'], w), wmed(h['g'], w), wmed(h['b'], w), float(np.average(h['blend'], weights=w))),
                             round(wmed(h['dist'], w), 0)])
    scn['items'].sort(key=lambda t: -t[1]); scn['items'] = scn['items'][:top_items + 4]
    bg = np.median([r['bg_start'][:3] for r in lv], axis=0).astype(int).tolist()
    return {'pal': [[*p['rgb'], p['blend']] for p in pal], 'bg': bg, 'slots': slots, 'scn': scn}

def style_name(names, used):
    sub = F[F['level'].isin(names)]; dm = sub.mean(numeric_only=True)
    hues = sub[[f'hue_{k * 30}' for k in range(12)]].mean().values
    tone = 'Dark' if dm['black_share'] > 0.35 or dm['bg_brightness'] < 0.2 else ('Bright' if dm['brightness'] > 0.7 else 'Soft')
    hue = HUES[int(np.argmax(hues))] if hues.max() > 0.04 and dm['vivid_share'] > 0.12 else ('White' if dm['white_share'] > 0.25 else 'Mono')
    m = F['level'].isin(names).values
    sig = (X[m].mean(0) / (X.mean(0) + 1e-6)) * (X[m] > 0).mean(0)
    word = None
    for i in np.argsort(-sig)[:25]:
        f_ = fam(d['deco_ids'][i]).lower()
        for key, wd in FAMILY_WORDS:
            if key in f_: word = wd; break
        if word: break
    nm = f'{tone} {hue} {word or "Detail"}'
    k = 2
    while nm in used: nm = f'{tone} {hue} {word or "Detail"} {k}'; k += 1
    used.add(nm); return nm

out = {'version': 1, 'styles': [], 'levels': []}
used = set(); report = ['# Styles learned from the level pack\n']
for c in sorted(F['style'].unique(), key=lambda c: -(F['style'] == c).sum()):
    names = F[F['style'] == c]['level'].tolist()
    nm = style_name(names, used)
    p = profile(names)
    p.update({'name': nm, 'from': sorted(names)})
    out['styles'].append(p)
    report.append(f'\n## {nm} ({len(names)} levels)\n\nLevels: {", ".join(sorted(names))}\n\n')
    report.append('Palette: ' + ', '.join('#%02x%02x%02x%s' % (q[0], q[1], q[2], ' (glow)' if q[3] else '') for q in p['pal']) + '\n\n')
    for s in p['slots'][:0]: pass
    top = sorted(p['slots'], key=lambda s: -s['rate'])[:8]
    report.append('| gameplay object | side | decorations per side | most used decoration |\n|---|---|---|---|\n')
    for s in top:
        report.append(f"| {s['a']} | {s['s']} | {s['rate']} | " + ', '.join(f"{it[0]} ({fam(it[0])}, {round(100 * it[1])}%)" for it in s['items'][:3]) + ' |\n')
for r in levels:
    p = profile([r['name']], top_items=6, min_share=0.03)
    p.update({'name': r['name']})
    out['levels'].append(p)
json.dump(out, open(R + '/styles_for_mod.json', 'w'), separators=(',', ':'))
open(R + '/08_styles_report.md', 'w').write(''.join(report))
F[['level', 'style']].assign(style_name=F['style'].map({c: None for c in F['style'].unique()})).to_csv(R + '/09_level_style_groups.csv', index=False)
for s in out['styles']: print(s['name'], len(s['from']), len(s['slots']), len(s['scn']['items']), s['pal'][:3])
import os; print('json size', os.path.getsize(R + '/styles_for_mod.json'))
