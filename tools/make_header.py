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
for s in d['styles'] + d['levels']:
    s['pal'] = [brighten(p) for p in s['pal']]
    s.pop('from', None) if False else None
d['cats'] = cats
js = json.dumps(d, separators=(',', ':'))
assert ')__json__"' not in js
with open(OUTH, 'w') as f:
    f.write('// Generated from the level pack analysis. Do not edit by hand.\n#pragma once\n\n')
    f.write('inline constexpr char STYLE_DATA_JSON[] = R"__json__(' + js + ')__json__";\n')
print('bytes', len(js), 'styles', len(d['styles']), 'levels', len(d['levels']))
