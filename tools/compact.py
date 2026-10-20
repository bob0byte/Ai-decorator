# Convert decoded level JSON into compact numpy arrays (objects) + trigger parameter tables.
import json, os, sys, numpy as np, pickle
P = sys.argv[1]; L = P + '/pack/levels'; O = P + '/pack/npz'; os.makedirs(O, exist_ok=True)
def f(o, k, d=0.0):
    v = o.get(k, d)
    try: return float(v)
    except Exception: return d
def hsv(o, k):
    v = o.get(k)
    if not isinstance(v, str): return (0.0, 1.0, 1.0)
    p = v.split('a')
    try: return (float(p[0]), float(p[1]), float(p[2]))
    except Exception: return (0.0, 1.0, 1.0)
TRIG_KEYS = ['1','2','3','6','7','8','9','10','17','23','28','29','30','35','45','46','47','48','49','50','51','52','56','57','62','63','65','66','68','69','70','71','75','84','85','86','87','10','90','91','97','107','120','128','129']
idx = {}
for fn in sorted(os.listdir(L)):
    lv = json.load(open(os.path.join(L, fn)))
    ob = lv['objects']; n = len(ob)
    a = np.zeros((n, 20), np.float32)
    trig = []
    for j, o in enumerate(ob):
        s = f(o, '32', 1.0) or 1.0
        h1 = hsv(o, '43') if o.get('41') in (1, '1') else (0.0, 1.0, 1.0)
        a[j] = (f(o,'1'), f(o,'2'), f(o,'3'), f(o,'6'), f(o,'4'), f(o,'5'), s, f(o,'128',1.0) or 1.0, f(o,'129',1.0) or 1.0,
                f(o,'24'), f(o,'25'), f(o,'21'), f(o,'22'), len(str(o.get('57','')).split('.')) if '57' in o else 0,
                h1[0], h1[1], h1[2], f(o,'20'), f(o,'121'), f(o,'96'))
        if '51' in o or '10' in o or '45' in o or '7' in o or o.get('1') in (1006,1007,901,1346,899,1268,1049,1520):
            trig.append({k: o[k] for k in o if k in TRIG_KEYS or k in ('1',)})
    name = lv['name']
    np.savez_compressed(os.path.join(O, fn.replace('.json', '.npz')), obj=a)
    pickle.dump({'name': name, 'colors': lv['colors'], 'header': lv['header'], 'triggers': trig}, open(os.path.join(O, fn.replace('.json', '.meta.pkl')), 'wb'))
    idx[fn] = (name, n)
    print(name, n, len(trig), flush=True)
pickle.dump(idx, open(O + '/index.pkl', 'wb'))
