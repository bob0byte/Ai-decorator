# Build object reference: id -> category, family, texture, hitbox, default z layer / color.
import json, re, csv, sys
P = sys.argv[1]
d = json.load(open(P + '/ref/gdclone/assets/data/object.json'))

TRIGGERS = {  # 2.2 trigger IDs (G.js constants + gd.py legacy color triggers + enter effects + 2.2 shader/area/etc.)
 'Start Pos':31,'Trail Enable':32,'Trail Disable':33,'BG Color (legacy)':29,'Ground Color (legacy)':30,'Line Color (legacy)':104,
 'Object Color (legacy)':105,'Color 1 (legacy)':221,'Color 2 (legacy)':717,'Color 3 (legacy)':718,'Color 4 (legacy)':743,
 '3DL Color (legacy)':744,'Ground 2 Color (legacy)':900,'Line 2 Color (legacy)':915,
 'Color':899,'Move':901,'Pulse':1006,'Alpha':1007,'Toggle':1049,'Spawn':1268,'Rotate':1346,'Follow':1347,'Shake':1520,
 'Animate':1585,'Touch':1595,'Count':1611,'Player Hide':1612,'Player Show':1613,'Stop':1616,'Instant Count':1811,
 'On Death':1812,'Follow Player Y':1814,'Collision':1815,'Pickup':1817,'BG Effect On':1818,'BG Effect Off':1819,
 'Random':1912,'Zoom Camera':1913,'Static Camera':1914,'Offset Camera':1916,'Reverse':1917,'End Wall':1931,
 'Player Control':1932,'Song':1934,'Time Warp':1935,'Rotate Camera':2015,'Camera Guide':2016,'Camera Edge':2062,
 'Checkpoint':2063,'Gravity':2066,'Scale':2067,'Advanced Random':2068,'Force Block':2069,'Options':2899,'Arrow':2900,
 'Gameplay Offset':2901,'Gradient':2903,'Camera Mode':2925,'Edit MG':2999,'Advanced Follow':3016,'Teleport':3022,
 'Change BG':3029,'Change Ground':3030,'Change MG':3031,'Keyframe':3032,'Animate Keyframe':3033,'End':3600,'SFX':3602,
 'Edit SFX':3603,'Event':3604,'Edit Song':3605,'BG Speed':3606,'Sequence':3607,'Spawn Particle':3608,
 'Instant Collision':3609,'MG Speed':3612,'UI':3613,'Time':3614,'Time Event':3615,'Time Control':3617,'Reset':3618,
 'Item Edit':3619,'Item Compare':3620,'State Block':3640,'Item Persist':3641,'BPM':3642,'Toggle Block':3643,
 'Force Circle':3645,'Object Control':3655,'Edit Advanced Follow':3660,'Retarget Advanced Follow':3661,'Link Visible':3662,
 # enter effect triggers
 'Enter Effect (fade)':22,'Enter Effect (bottom)':23,'Enter Effect (top)':24,'Enter Effect (left)':25,'Enter Effect (right)':26,
 'Enter Effect (scale in)':27,'Enter Effect (scale out)':28,'Enter Effect (chaotic)':55,'Enter Effect (left chaotic)':56,
 'Enter Effect (right chaotic)':57,'Enter Effect (left far)':58,'Enter Effect (right far)':59,'Enter Effect (none)':1915,
 # 2.2 shader triggers
 'Shader Setup':2904,'Shock Wave':2905,'Shock Line':2907,'Glitch':2909,'Chromatic':2910,'Chromatic Glitch':2911,
 'Pixelate':2912,'Lens Circle':2913,'Radial Blur':2914,'Motion Blur':2915,'Bulge':2916,'Pinch':2917,'Gray Scale':2919,
 'Sepia':2920,'Invert Color':2921,'Hue':2922,'Edit Color (shader)':2923,'Split Screen':2924,
 # 2.2 area triggers
 'Area Move':3006,'Area Rotate':3007,'Area Scale':3008,'Area Fade':3009,'Area Tint':3010,'Edit Area Move':3011,
 'Edit Area Rotate':3012,'Edit Area Scale':3013,'Edit Area Fade':3014,'Edit Area Tint':3015,'Area Stop':3024,
 'Enter Move':3017,'Enter Rotate':3018,'Enter Scale':3019,'Enter Fade':3020,'Enter Tint':3021,'Enter Stop':3023,
}
TRIG = {v: k for k, v in TRIGGERS.items()}
ORBS = {36:'Yellow Orb',84:'Blue Orb',141:'Pink Orb',1022:'Green Orb',1333:'Red Orb',1330:'Black Orb',1704:'Dash Orb',1751:'Reverse Dash Orb',1594:'Toggle Orb',3004:'Spider Orb',3027:'Teleport Orb'}
PADS = {35:'Yellow Pad',67:'Blue Pad',140:'Pink Pad',1332:'Red Pad',3005:'Spider Pad'}
PORTALS = {12:'Cube',13:'Ship',47:'Ball',111:'UFO',660:'Wave',745:'Robot',1331:'Spider',1933:'Swing',11:'Gravity Up',10:'Gravity Down',
           2926:'Gravity Toggle',45:'Mirror On',46:'Mirror Off',101:'Mini',99:'Normal Size',286:'Dual On',287:'Dual Off',747:'Teleport',749:'Teleport (exit)',2902:'Teleport (2.2)'}
SPEEDS = {200:'Speed 0.5x',201:'Speed 1x',202:'Speed 2x',203:'Speed 3x',1334:'Speed 4x'}
COLLECT = {142:'Secret Coin',1329:'User Coin',1275:'Key',1587:'Heart',1589:'Potion',1598:'Skull',1614:'Small Coin'}

def family(tex):
    t = tex.lower().replace('.png', '')
    t = re.sub(r'(_color)?_?\d{3}$', '', t)
    t = re.sub(r'_\d+$', '', t)
    t = re.sub(r'_\d+', '', t)
    return t or 'unknown'

rows = []
ids = set(int(k) for k in d) | set(TRIG) | set(ORBS) | set(PADS) | set(PORTALS) | set(SPEEDS) | set(COLLECT)
for i in sorted(ids):
    v = d.get(str(i), {})
    tex = v.get('texture', '')
    h = v.get('hitbox')
    fam = family(tex) if tex else ''
    if tex == 'emptyFrame.png' and v.get('children'):
        kids = sorted(set(family(c['texture']) for c in v['children'] if c.get('texture')))
        fam = '+'.join(kids[:3]) or fam
    cat = 'deco'; name = ''
    if i in TRIG: cat, name = 'trigger', TRIG[i]
    elif i in ORBS: cat, name = 'orb', ORBS[i]
    elif i in PADS: cat, name = 'pad', PADS[i]
    elif i in PORTALS: cat, name = 'portal', PORTALS[i]
    elif i in SPEEDS: cat, name = 'speed', SPEEDS[i]
    elif i in COLLECT: cat, name = 'collectible', COLLECT[i]
    elif h:
        t = h['type']; f = fam
        if t == 'Circle': cat = 'saw'
        elif t == 'Slope': cat = 'spike_slope' if tex == 'emptyFrame.png' else 'slope'
        elif f.startswith('pit'): cat = 'ground_spike'
        elif 'spike' in f: cat = 'spike'
        elif f.startswith('edit_e') or tex == 'emptyFrame.png': cat = 'special'
        elif h.get('height', 30) <= 16 or h.get('width', 30) <= 16: cat = 'platform'
        else: cat = 'block'
        if 'invis' in f and cat in ('block', 'platform', 'spike', 'slope'): name = 'invisible'
    rows.append({'id': i, 'category': cat, 'name': name, 'family': fam, 'texture': tex,
                 'hitbox': (h or {}).get('type', ''), 'hb_w': (h or {}).get('width', (h or {}).get('radius', '')),
                 'hb_h': (h or {}).get('height', ''), 'z_layer': v.get('default_z_layer', ''),
                 'z_order': v.get('default_z_order', ''), 'color': v.get('default_base_color_channel', ''),
                 'detail_color': v.get('default_detail_color_channel', ''), 'color_type': v.get('color_type', ''),
                 'children': len(v.get('children', []))})
with open(P + '/pack/objects_reference.csv', 'w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
from collections import Counter
print(len(rows), Counter(r['category'] for r in rows))
