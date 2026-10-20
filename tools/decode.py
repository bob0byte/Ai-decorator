# Decode GD .gmd files into per-level JSON: header (colors, settings) + object list.
import sys, re, base64, gzip, zlib, json, os, html
src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)

def b64(s):
    s = s.strip().replace('-', '+').replace('_', '/')
    s += '=' * (-len(s) % 4)
    return base64.b64decode(s)

def inflate(b):
    try: return gzip.decompress(b)
    except Exception: return zlib.decompress(b, 15 + 32)

def parse_kv(s, sep):
    p = s.split(sep); d = {}
    for i in range(0, len(p) - 1, 2): d[p[i]] = p[i + 1]
    return d

def num(v):
    try:
        f = float(v); return int(f) if f.is_integer() and 'e' not in v.lower() else f
    except Exception: return v

summary = []
for fn in sorted(os.listdir(src)):
    path = os.path.join(src, fn)
    try:
        txt = open(path, encoding='utf-8', errors='replace').read()
        meta = dict(re.findall(r'<k>(k\d+|kCEK)</k><[si]>([^<]*)</[si]>', txt))
        ls = meta.get('k4')
        if not ls: raise ValueError('no level string (k4)')
        raw = ls if ls.startswith('kS38') or ls.startswith('kA') else inflate(b64(html.unescape(ls))).decode('utf-8', 'replace')
        parts = raw.split(';')
        header = parse_kv(parts[0], ',')
        colors = []
        if 'kS38' in header:
            for c in header['kS38'].split('|'):
                if c: colors.append({k: num(v) for k, v in parse_kv(c, '_').items()})
        objs = []
        for o in parts[1:]:
            if not o: continue
            d = parse_kv(o, ',')
            objs.append({k: num(v) for k, v in d.items()})
        name = html.unescape(meta.get('k2', os.path.splitext(fn)[0]))
        json.dump({'name': name, 'file': fn, 'meta': {k: v for k, v in meta.items() if k != 'k4' and len(v) < 300},
                   'header': {k: v for k, v in header.items() if k != 'kS38'}, 'colors': colors, 'objects': objs},
                  open(os.path.join(out, re.sub(r'[^\w\- ]', '_', name) + '.json'), 'w'))
        summary.append((name, len(objs)))
    except Exception as e:
        summary.append((fn, f'ERROR {e}'))
for s in summary: print(*s, sep='\t')
