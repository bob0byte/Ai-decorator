import pickle, csv, sys, json, collections, numpy as np
P = sys.argv[1]; R = P + '/pack/results'
rows = pickle.load(open(P + '/pack/triggers_all.pkl', 'rb'))
ref = {int(r['id']): r for r in csv.DictReader(open(P + '/pack/objects_reference.csv'))}
by = collections.defaultdict(list)
for r in rows: by[r['1']].append(r)
NAMES = {'10': 'duration', '7': 'red', '8': 'green', '9': 'blue', '17': 'blending', '35': 'opacity', '23': 'target color',
         '28': 'move x', '29': 'move y', '30': 'easing', '85': 'easing rate', '45': 'fade in', '46': 'hold', '47': 'fade out',
         '48': 'pulse HSV mode', '52': 'pulse targets group', '65': 'main only', '66': 'detail only', '86': 'exclusive',
         '68': 'degrees', '69': 'times 360', '70': 'lock rotation', '75': 'strength', '84': 'interval', '56': 'activate group',
         '63': 'spawn delay', '62': 'spawn triggered', '87': 'multi trigger', '11': 'touch triggered', '50': 'copy color',
         '49': 'copied HSV', '58': 'lock player X', '59': 'lock player Y', '100': 'move to target', '150': 'scale x', '151': 'scale y',
         '153': 'div by x', '154': 'div by y', '71': 'center / target group', '51': 'target group', '120': 'time mod'}
EASE = ['None', 'Ease In Out', 'Ease In', 'Ease Out', 'Elastic In Out', 'Elastic In', 'Elastic Out', 'Bounce In Out', 'Bounce In',
        'Bounce Out', 'Exp In Out', 'Exp In', 'Exp Out', 'Sine In Out', 'Sine In', 'Sine Out', 'Back In Out', 'Back In', 'Back Out']
def fnum(v):
    try: return float(v)
    except Exception: return None
out = []; md = ['# Triggers in the level pack\n', f'{len(rows):,} triggers across {len(set(r["level"] for r in rows))} levels.\n',
               '\n| trigger | id | count | levels | spawn/touch-triggered % | most common settings |\n|---|---|---|---|---|---|\n']
presets = {}
for tid, lst in sorted(by.items(), key=lambda kv: -len(kv[1])):
    name = ref.get(tid, {}).get('name', '') or f'2.2 trigger {tid}'
    levels = len(set(r['level'] for r in lst))
    st = sum(1 for r in lst if r.get('62') in (1, '1') or r.get('11') in (1, '1')) / len(lst)
    keyc = collections.Counter(k for r in lst for k in r if k not in ('level', '1', '2', '3', '155', '156', '36', '20', '61', '57', '64', '67', '108'))
    stats = {}
    for k, c in keyc.most_common(14):
        if c < 0.05 * len(lst): continue
        vals = [fnum(r[k]) for r in lst if k in r]; vals = [v for v in vals if v is not None]
        if not vals: continue
        a = np.array(vals); mode = collections.Counter(np.round(a, 2)).most_common(3)
        stats[k] = {'name': NAMES.get(k, f'key {k}'), 'present_pct': round(100 * c / len(lst), 1), 'median': float(np.median(a)),
                    'p10': float(np.percentile(a, 10)), 'p90': float(np.percentile(a, 90)),
                    'top_values': [(float(v), round(100 * n / len(a), 1)) for v, n in mode]}
    if '30' in stats: stats['30']['top_values'] = [(EASE[int(v)] if 0 <= v < len(EASE) else v, p) for v, p in stats['30']['top_values']]
    out.append({'id': tid, 'name': name, 'count': len(lst), 'levels': levels, 'spawn_or_touch_pct': round(100 * st, 1), 'settings': stats})
    summ = '; '.join(f"{s['name']}: {', '.join(str(v) for v, _ in s['top_values'][:2])}" for s in list(stats.values())[:6])
    md.append(f'| {name} | {tid} | {len(lst):,} | {levels} | {round(100 * st)} | {summ} |\n')
json.dump(out, open(R + '/07_triggers.json', 'w'), indent=1, default=str)
open(R + '/07_triggers.md', 'w').write(''.join(md))
for o in out[:25]:
    print(o['name'], o['id'], o['count'], o['levels'], o['spawn_or_touch_pct'])
    for k, s in list(o['settings'].items())[:7]: print('   ', s['name'], s['present_pct'], 'median', round(s['median'], 2), s['top_values'])
