# Combine per-level analyses: global sorted tables, level features, style clusters, style profiles.
import os, sys, csv, pickle, json, colorsys, math
import numpy as np, pandas as pd
from sklearn.cluster import KMeans
from sklearn.metrics import silhouette_score
from sklearn.preprocessing import StandardScaler
from sklearn.decomposition import TruncatedSVD

P = sys.argv[1]
A = P + '/pack/analysis'; OUT = P + '/pack/results'; os.makedirs(OUT, exist_ok=True)
ref = {int(r['id']): r for r in csv.DictReader(open(P + '/pack/objects_reference.csv'))}
def fam(i): return ref[i]['family'] if i in ref else '2.2 object'
def cat(i): return ref[i]['category'] if i in ref else ('deco (2.2)' if i > 1911 else 'unknown')

levels = []
for fn in sorted(os.listdir(A)):
    r = pickle.load(open(os.path.join(A, fn), 'rb'))
    if 'attached' in r: levels.append(r)
print('levels', len(levels))

# ------------------------------------------------------------------ 1. object usage
use = {}
for r in levels:
    for i, n in r['id_counts'].items():
        u = use.setdefault(int(i), [0, 0]); u[0] += n; u[1] += 1
rows = [{'id': i, 'category': cat(i), 'name': ref.get(i, {}).get('name', ''), 'family': fam(i),
         'texture': ref.get(i, {}).get('texture', ''), 'total_uses': n, 'levels_using': k,
         'pct_of_levels': round(100 * k / len(levels), 1)} for i, (n, k) in use.items()]
obj_usage = pd.DataFrame(rows).sort_values(['category', 'total_uses'], ascending=[True, False])
obj_usage.to_csv(OUT + '/01_object_usage.csv', index=False)

# ------------------------------------------------------------------ 2. decoration by anchor/side (global)
att = []
for r in levels:
    a = r['attached'].copy(); a['level'] = r['name']; att.append(a)
att = pd.concat(att, ignore_index=True)
att = att[att['deco_id'] > 0]
denom_cat = {}
for r in levels:
    for c, n in r['anchor_counts'].items(): denom_cat[c] = denom_cat.get(c, 0) + n

def wmed(v, w):
    o = np.argsort(v); v, w = np.asarray(v)[o], np.asarray(w)[o]; c = np.cumsum(w)
    return float(v[np.searchsorted(c, c[-1] / 2)])
def wmode(v, w):
    s = pd.Series(w).groupby(np.asarray(v)).sum(); return s.idxmax()

def combine(g):
    w = g['n'].values
    return pd.Series({
        'uses': int(w.sum()), 'levels': g['level'].nunique(),
        'per_100_anchors': round(100 * w.sum() / max(1, denom_cat.get(g.name[0], 1)), 3),
        'offset_along': round(wmed(g['off_t'], w), 1), 'offset_out': round(wmed(g['off_n'], w), 1),
        'rotation': wmode(g['rot'], w), 'scale_x': round(wmed(g['sx'], w), 2), 'scale_y': round(wmed(g['sy'], w), 2),
        'flip_x_pct': round(100 * np.average(g['flipx'], weights=w), 1), 'z_layer': wmode(g['z'], w),
        'z_order': round(wmed(g['zo'], w), 1), 'blending_pct': round(100 * np.average(g['blend'], weights=w), 1),
        'grouped_pct': round(100 * np.average(g['grouped'], weights=w), 1),
        'hsv_pct': round(100 * np.average(g['hsv'], weights=w), 1),
        'typical_rgb': '#%02x%02x%02x' % tuple(int(wmed(g[c], w)) for c in 'rgb')})

by_anchor = att.groupby(['anchor_cat', 'side', 'exposed', 'deco_id']).apply(combine).reset_index()
by_anchor['deco_family'] = by_anchor['deco_id'].map(fam)
by_anchor['deco_category'] = by_anchor['deco_id'].map(cat)
by_anchor = by_anchor.sort_values(['anchor_cat', 'side', 'exposed', 'uses'], ascending=[True, True, False, False])
by_anchor.to_csv(OUT + '/02_decoration_by_anchor_and_side.csv', index=False)

# readable per-anchor-category sheets (top 40 per side)
SIDE_ORDER = ['center', 'top', 'bottom', 'left', 'right', 'corner', 'corner_low']
with open(OUT + '/03_decoration_sorted_by_gameplay_object.md', 'w') as f:
    for ac in ['block', 'platform', 'slope', 'spike', 'ground_spike', 'spike_slope', 'saw', 'orb', 'pad', 'portal', 'speed', 'collectible']:
        sub = by_anchor[by_anchor['anchor_cat'] == ac]
        if sub.empty: continue
        f.write(f'\n# {ac.replace("_", " ").title()} decoration ({denom_cat.get(ac, 0):,} {ac} objects in the pack)\n')
        for sd in SIDE_ORDER:
            for ex in (True, False):
                s2 = sub[(sub['side'] == sd) & (sub['exposed'] == ex)].head(40)
                if s2.empty or s2['uses'].sum() < 50: continue
                f.write(f'\n## {sd}{"" if ex else " (touching another block)"}\n\n')
                f.write('| deco id | family | uses | levels | per 100 | along | out | rot | scale | z | blend% | color |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n')
                for _, t in s2.iterrows():
                    f.write(f"| {t.deco_id} | {t.deco_family} | {t.uses} | {t.levels} | {t.per_100_anchors} | {t.offset_along} | {t.offset_out} | {t.rotation:g} | {t.scale_x}x{t.scale_y} | {t.z_layer} | {t.blending_pct} | {t.typical_rgb} |\n")

# ------------------------------------------------------------------ 3. free-floating scenery (global)
fr = []
for r in levels:
    if 'free' in r:
        q = r['free'].copy(); q['level'] = r['name']; fr.append(q)
fr = pd.concat(fr, ignore_index=True)
free = fr.groupby('deco_id').apply(lambda g: pd.Series({
    'uses': int(g['n'].sum()), 'levels': g['level'].nunique(), 'scale_x': round(wmed(g['sx'], g['n']), 2),
    'scale_y': round(wmed(g['sy'], g['n']), 2), 'rotation': wmode(g['rot_mode'], g['n']), 'z_layer': wmode(g['z'], g['n']),
    'distance_from_gameplay': round(wmed(g['dist'], g['n']), 0), 'blending_pct': round(100 * np.average(g['blend'], weights=g['n']), 1),
    'grouped_pct': round(100 * np.average(g['grouped'], weights=g['n']), 1),
    'typical_rgb': '#%02x%02x%02x' % tuple(int(wmed(g[c], g['n'])) for c in 'rgb')})).reset_index()
free['family'] = free['deco_id'].map(fam)
free.sort_values('uses', ascending=False).to_csv(OUT + '/04_background_scenery.csv', index=False)

# ------------------------------------------------------------------ 4. per-level features & clustering
deco_ids = obj_usage[(obj_usage['category'].isin(['deco', 'deco (2.2)'])) & (obj_usage['levels_using'] >= 5)]['id'].tolist()
col = {i: k for k, i in enumerate(deco_ids)}
X_ids = np.zeros((len(levels), len(deco_ids)))
feat = []
for li, r in enumerate(levels):
    tot = sum(n for i, n in r['id_counts'].items() if int(i) in col) or 1
    for i, n in r['id_counts'].items():
        if int(i) in col: X_ids[li, col[int(i)]] = n / tot
    dc = r['deco_colors']; w = np.array(list(dc.values()), float); cs = np.array(list(dc.keys()), float)
    hsv = np.array([colorsys.rgb_to_hsv(*(c[:3] / 255)) for c in cs])
    hue_hist = np.zeros(12); vivid = (hsv[:, 1] > .35) & (hsv[:, 2] > .3)
    for (h, s_, v), ww, vv in zip(hsv, w, vivid):
        if vv: hue_hist[int(h * 12) % 12] += ww
    hue_hist /= max(1, w.sum())
    n_anchor = sum(r['anchor_counts'].values()) or 1
    zh = r['flags']['z_hist']; zt = sum(zh.values()) or 1
    feat.append({
        'level': r['name'], 'objects': r['n'],
        'deco_per_gameplay': (r['n_attached'] + r['n_free']) / n_anchor,
        'attached_share': r['n_attached'] / max(1, r['n_attached'] + r['n_free']),
        'black_share': float(w[(cs[:, :3].max(1) < 20)].sum() / w.sum()),
        'white_share': float(w[(cs[:, :3].min(1) > 230)].sum() / w.sum()),
        'vivid_share': float(w[vivid].sum() / w.sum()),
        'brightness': float(np.average(hsv[:, 2], weights=w)), 'saturation': float(np.average(hsv[:, 1], weights=w)),
        'blending_share': float(w[cs[:, 3] > 0].sum() / w.sum()),
        'bg_brightness': max(r['bg'][:3]) / 255, 'grouped_deco': r['flags']['grouped_deco'], 'hsv_deco': r['flags']['hsv_deco'],
        'bg_layers': sum(v for k, v in zh.items() if k < 0) / zt, 'front_layers': sum(v for k, v in zh.items() if k > 3) / zt,
        **{f'hue_{k * 30}': hue_hist[k] for k in range(12)}})
F = pd.DataFrame(feat)
svd = TruncatedSVD(n_components=12, random_state=0).fit_transform(np.sqrt(X_ids))
num = F.drop(columns=['level', 'objects']).values
Z = np.hstack([StandardScaler().fit_transform(num), StandardScaler().fit_transform(svd) * 0.8])
best = None
for k in range(6, 13):
    km = KMeans(n_clusters=k, n_init=20, random_state=0).fit(Z)
    sc = silhouette_score(Z, km.labels_)
    if best is None or sc > best[0]: best = (sc, k, km)
sc, K, km = best
F['style'] = km.labels_
print('clusters', K, 'silhouette', round(sc, 3))

# ------------------------------------------------------------------ 5. describe & name each cluster
HUES = ['red', 'orange', 'yellow', 'lime', 'green', 'teal', 'cyan', 'azure', 'blue', 'purple', 'magenta', 'pink']
glob_share = X_ids.mean(0) + 1e-6
styles = []
for c in range(K):
    m = F['style'] == c; sub = F[m]
    hues = sub[[f'hue_{k * 30}' for k in range(12)]].mean().values
    top_h = [HUES[i] for i in np.argsort(-hues)[:2] if hues[i] > 0.03]
    sig = (X_ids[m.values].mean(0) / glob_share) * (X_ids[m.values] > 0).mean(0)
    sig_ids = [deco_ids[i] for i in np.argsort(-sig)[:12]]
    fams = []
    for i in sig_ids:
        f_ = fam(i)
        if f_ not in fams: fams.append(f_)
    d = sub.mean(numeric_only=True)
    words = []
    words.append('Dark' if d['black_share'] > 0.35 or d['bg_brightness'] < 0.2 else ('Bright' if d['brightness'] > 0.75 else 'Mid-tone'))
    if d['vivid_share'] > 0.25 and top_h: words.append('/'.join(top_h))
    elif d['white_share'] > 0.3: words.append('white')
    if d['blending_share'] > 0.35: words.append('glow')
    words.append('dense' if d['deco_per_gameplay'] > np.median(F['deco_per_gameplay']) * 1.6 else
                 ('minimal' if d['deco_per_gameplay'] < np.median(F['deco_per_gameplay']) * 0.6 else 'detailed'))
    styles.append({'style': c, 'auto_name': ' '.join(words), 'levels': sub['level'].tolist(), 'n_levels': int(m.sum()),
                   'signature_families': fams[:6], 'signature_ids': sig_ids, 'means': d.round(3).to_dict()})
F.to_csv(OUT + '/05_level_features_and_style.csv', index=False)
json.dump(styles, open(OUT + '/06_styles.json', 'w'), indent=1, default=float)
pickle.dump({'att': att, 'free': fr, 'levels': levels, 'F': F, 'styles': styles, 'deco_ids': deco_ids, 'X_ids': X_ids},
            open(OUT + '/agg.pkl', 'wb'))
for s in styles: print(s['style'], s['n_levels'], s['auto_name'], s['signature_families'][:4], s['levels'][:6])
