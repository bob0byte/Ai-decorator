# Turn styles_for_mod.json into src/StyleData.hpp (embedded so the mod needs no extra files).
import json, csv, sys, colorsys
P, OUTH = sys.argv[1], sys.argv[2]
d = json.load(open(P + '/pack/results/styles_for_mod.json'))
ref = list(csv.DictReader(open(P + '/pack/objects_reference.csv')))
cats = {}
for r in ref:
    if r['category'] in ('block', 'platform', 'slope', 'spike', 'ground_spike', 'spike_slope', 'saw', 'orb', 'pad', 'portal', 'speed', 'collectible'):
        cats.setdefault(r['category'], []).append(int(r['id']))
def brighten(p):
    r, g, b, bl = p
    if bl and max(r, g, b) < 200:
        h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
        r, g, b = [int(round(c * 255)) for c in colorsys.hsv_to_rgb(h, max(s, 0.35) if v > 0.02 else 0, 0.85)]
    return [r, g, b, bl]
fam = {int(r['id']): r['family'].lower() for r in ref}
FAMWORDS = [('chain', 'chains'), ('persp', '3d'), ('gridline', 'grid lines'), ('small_ball', 'particles dots'), ('ball', 'orbs balls'),
            ('scalefadering', 'rings circles'), ('gradient', 'glow neon'), ('drops', 'drips'), ('pixel', 'pixel'), ('cloud', 'clouds'),
            ('outline', 'outlines'), ('spike', 'spikes thorns'), ('circle', 'circles'), ('block0', 'textured blocks'), ('blockdesign', 'textured blocks'),
            ('lightsquare', 'squares'), ('whiteblock', 'squares'), ('light', 'light')]
HUE = ['red', 'orange', 'yellow', 'lime', 'green', 'teal', 'cyan', 'azure', 'blue', 'purple', 'magenta', 'pink']
def tags_for(s):
    t = {}
    def add(w, v):
        for x in w.split(): t[x] = t.get(x, 0) + v
    vals = []
    for r, g, b, bl in s['pal']:
        h, sat, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255); vals.append(v)
        if sat > 0.35 and v > 0.25: add(HUE[int(h * 12) % 12], 1.0)
        if bl: add('glow', 0.3)
    if s['pal'] and s['pal'][0][:3] and max(s['pal'][0][:3]) < 40: add('dark black', 1.0)
    elif vals and sum(vals) / len(vals) > 0.6: add('bright light', 1.0)
    if any(min(p[:3]) > 220 for p in s['pal']): add('white', 0.6)
    fw = {}
    for sl in s['slots']:
        for it in sl['items']:
            f = fam.get(it[0], '')
            for key, words in FAMWORDS:
                if key in f: fw[words] = fw.get(words, 0) + it[1] * sl['rate']; break
    tot = sum(fw.values()) or 1
    for words, v in fw.items(): add(words, round(2 * v / tot, 3))
    rate = sum(sl['rate'] for sl in s['slots']) / max(1, len(s['slots']))
    add('dense detailed busy' if rate > 1.2 else ('minimal clean simple' if rate < 0.5 else 'detailed'), 0.8)
    for w in s['name'].lower().replace('-', ' ').split(): t[w] = t.get(w, 0) + 3
    return {k: round(v, 3) for k, v in t.items() if v >= 0.05}
for s in d['styles'] + d['levels']:
    s['pal'] = [brighten(p) for p in s['pal']]
    s['tags'] = tags_for(s)
    s.pop('from', None) if False else None
# community deco types (classify.py) and effects (effects.py)
types = json.load(open(P + '/pack/results/10_deco_types.json'))['types']
eff = json.load(open(P + '/pack/results/11_level_effects.json'))
idx = {s['name']: i for i, s in enumerate(d['levels'])}
TYPE_WORDS = {'Glow': 'glow glowing gradient', 'Modern': 'modern flat clean', 'Nine Circles': 'nine circles nc wave diamond',
              'Effect': 'effect effects moving animated', 'Art': 'art detailed artistic custom', 'Classic': 'classic tilemap textured old',
              '3D': '3d depth perspective', 'Hell': 'hell fire lava demon red', 'Retro': 'retro pixel green computer',
              'Particles': 'particles sparkles circles orbs', 'Minimal': 'minimal simple clean'}
d['types'] = []
for t, names in sorted(types.items(), key=lambda kv: -len(kv[1])):
    d['types'].append({'name': t, 'levels': [idx[n] for n in names if n in idx]})
    for n in names:
        if n in idx:
            lv = d['levels'][idx[n]]; lv['type'] = t
            for w in TYPE_WORDS[t].split(): lv['tags'][w] = lv['tags'].get(w, 0) + 2.0
for lv in d['levels']:
    e = eff.get(lv['name'], {})
    lv['eff'] = [round(e.get('pulse_gap', 0) or 123.0, 1), round(e.get('fade_in', 0), 3), round(e.get('hold', 0), 3),
                 round(e.get('fade_out', 0.3) or 0.3, 3), int(e.get('pulses', 0))]
d.pop('styles', None)
d['cats'] = cats
js = json.dumps(d, separators=(',', ':'))
assert ')__json__"' not in js
with open(OUTH, 'w') as f:
    f.write('// Generated from the level pack analysis. Do not edit by hand.\n#pragma once\n\n')
    f.write('inline constexpr char STYLE_DATA_JSON[] = R"__json__(' + js + ')__json__";\n')
print([(t['name'], len(t['levels'])) for t in d['types']]); print(d['levels'][0]['name'], d['levels'][0]['tags']); print('bytes', len(js), 'levels', len(d['levels']))
