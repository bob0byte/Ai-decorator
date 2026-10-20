import json, os, sys, collections, pickle
D=sys.argv[1]
ids=collections.Counter(); per_level={}; keys=collections.Counter()
for fn in sorted(os.listdir(D+'/levels')):
    L=json.load(open(D+'/levels/'+fn))
    c=collections.Counter()
    for o in L['objects']:
        i=o.get('1')
        if isinstance(i,int): c[i]+=1
        keys.update(o.keys())
    ids.update(c); per_level[L['name']]=c
pickle.dump((ids,per_level,keys),open(D+'/idcount.pkl','wb'))
tot=sum(ids.values())
old=sum(v for k,v in ids.items() if k<=1911)
print('total',tot,'<=1911',old, 'distinct',len(ids),'distinct>1911',sum(1 for k in ids if k>1911))
print('top new:',[(k,v) for k,v in ids.most_common() if k>1911][:40])
