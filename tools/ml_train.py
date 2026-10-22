# Train the decoration network (numpy MLP, multi-label sigmoid output) and export it for the mod.
import sys, pickle, json, time, collections
import numpy as np

P = sys.argv[1]; EPOCHS = int(sys.argv[2]) if len(sys.argv) > 2 else 12
D = pickle.load(open(P + '/pack/ml/train_raw.pkl', 'rb'))
samples, tc, tstats, TYPES, HB, R = D['samples'], D['token_count'], D['token_stats'], D['types'], D['hb'], D['R']
V = 512
vocab = [t for t, _ in tc.most_common(V)]
tok_index = {t: i for i, t in enumerate(vocab)}
NB = (2 * R + 1) ** 2
NIN = len(HB) + 5 + 4 + len(TYPES) + NB * 4
print('samples', len(samples), 'inputs', NIN, 'vocab', len(vocab), flush=True)

# --- encode
N = len(samples)
hb = np.array([s[0] for s in samples], np.int16); side = np.array([s[1] for s in samples], np.int16)
q = np.array([s[2] for s in samples], np.int16); lt = np.array([s[3] for s in samples], np.int16)
nb = np.frombuffer(b''.join(s[4] for s in samples), np.int8).reshape(N, NB)
Y_idx = [[tok_index[t] for t in s[5] if t in tok_index] for s in samples]
del samples

def encode(rows):
    X = np.zeros((len(rows), NIN), np.float32)
    o = 0
    X[np.arange(len(rows)), o + hb[rows]] = 1; o += len(HB)
    X[np.arange(len(rows)), o + side[rows]] = 1; o += 5
    X[np.arange(len(rows)), o + q[rows]] = 1; o += 4
    X[np.arange(len(rows)), o + lt[rows]] = 1; o += len(TYPES)
    cells = nb[rows].astype(np.int64)
    r, c = np.nonzero(cells)
    X[r, o + c * 4 + (cells[r, c] - 1)] = 1
    return X
def labels(rows):
    Y = np.zeros((len(rows), V), np.float32)
    for k, i in enumerate(rows):
        if Y_idx[i]: Y[k, Y_idx[i]] = 1
    return Y

# split by anchor groups: validation = last 8% of samples (later levels in file order)
idx = np.arange(N); split = int(N * 0.92)
tr, va = idx[:split], idx[split:]
rng = np.random.default_rng(0)

# --- model
H1, H2 = 384, 256
def init(i, o): return (rng.standard_normal((i, o)) * np.sqrt(2 / i)).astype(np.float32), np.zeros(o, np.float32)
W1, b1 = init(NIN, H1); W2, b2 = init(H1, H2); W3, b3 = init(H2, V)
prior = np.clip(np.array([tc[t] for t in vocab], float) / (N / 5 * 5), 1e-5, 0.5)
b3[:] = np.log(prior / (1 - prior)).astype(np.float32)  # start at base rates
params = [W1, b1, W2, b2, W3, b3]
m = [np.zeros_like(p) for p in params]; v = [np.zeros_like(p) for p in params]
step = 0
def forward(X):
    h1 = np.maximum(X @ W1 + b1, 0); h2 = np.maximum(h1 @ W2 + b2, 0)
    return h1, h2, h2 @ W3 + b3
def sigmoid(z): return 1 / (1 + np.exp(-np.clip(z, -30, 30)))

def evaluate(rows, k=3):
    hits = tot_pred = tot_true = 0; ll = 0.0; nall = 0
    for s in range(0, len(rows), 8192):
        r = rows[s:s + 8192]; X = encode(r); Y = labels(r)
        _, _, z = forward(X); p = sigmoid(z)
        ll += float(-(Y * np.log(p + 1e-7) + (1 - Y) * np.log(1 - p + 1e-7)).sum()); nall += Y.size
        top = np.argsort(-p, axis=1)[:, :k]
        has = Y.sum(1) > 0
        for i in np.nonzero(has)[0]:
            hits += int(Y[i, top[i]].sum()); tot_pred += k; tot_true += int(min(k, Y[i].sum()))
    return ll / nall, hits / max(1, tot_true)

BATCH, LR0 = 1024, 2e-3
best_vl = 1e9
print('baseline (start) val loss/recall@3:', evaluate(va[:60000]), flush=True)
for ep in range(EPOCHS):
    t0 = time.time(); perm = rng.permutation(tr); lr = LR0 * (0.5 ** ep); tl = 0; nb_ = 0
    for s in range(0, len(perm), BATCH):
        r = perm[s:s + BATCH]; X = encode(r); Y = labels(r)
        h1, h2, z = forward(X); p = sigmoid(z)
        # BCE, positives weighted x3 (they are rare)
        w = 1 + 2 * Y
        g3 = (p - Y) * w / len(r)
        tl += float((-(Y * np.log(p + 1e-7) + (1 - Y) * np.log(1 - p + 1e-7)) * w).mean()); nb_ += 1
        gW3 = h2.T @ g3; gb3 = g3.sum(0)
        g2 = (g3 @ W3.T) * (h2 > 0); gW2 = h1.T @ g2; gb2 = g2.sum(0)
        g1 = (g2 @ W2.T) * (h1 > 0); gW1 = X.T @ g1; gb1 = g1.sum(0)
        step += 1
        for i, g in enumerate([gW1, gb1, gW2, gb2, gW3, gb3]):
            if i % 2 == 0: g = g + 1e-4 * params[i]  # weight decay
            m[i] = 0.9 * m[i] + 0.1 * g; v[i] = 0.999 * v[i] + 0.001 * g * g
            mh = m[i] / (1 - 0.9 ** step); vh = v[i] / (1 - 0.999 ** step)
            params[i] -= lr * mh / (np.sqrt(vh) + 1e-8)
    vl, rec = evaluate(va[:60000])
    if ep == 0 or vl < best_vl:
        best_vl = vl
        pickle.dump({'W': [p.copy() for p in params]}, open(P + '/pack/ml/ckpt.pkl', 'wb'))
    print(f'epoch {ep + 1}/{EPOCHS} train {tl / nb_:.4f} val loss {vl:.4f} val recall@3 {rec:.3f} lr {lr:.5f} {time.time() - t0:.0f}s', flush=True)

# frequency baseline for comparison: per (hb, side, type) most common tokens
cnt = collections.defaultdict(collections.Counter)
for i in tr[::3]:
    for t in Y_idx[i]: cnt[(hb[i], side[i], lt[i])][t] += 1
hits = tot = 0
for i in va[:60000]:
    if not Y_idx[i]: continue
    top = [t for t, _ in cnt[(hb[i], side[i], lt[i])].most_common(3)]
    hits += len(set(top) & set(Y_idx[i])); tot += min(3, len(Y_idx[i]))
print('frequency-table baseline recall@3:', round(hits / max(1, tot), 3), flush=True)

# --- token table: median placement for each token
toks = []
for t in vocab:
    st = np.array(tstats[t])
    toks.append([int(t[0]), round(float(np.median(st[:, 0])), 1), round(float(np.median(st[:, 1])), 1), float(t[1]),
                 round(float(np.median(st[:, 3])), 2), round(float(np.median(st[:, 4])), 2), int(np.median(st[:, 5])), int(np.median(st[:, 6]))])
json.dump({'nin': NIN, 'h1': H1, 'h2': H2, 'v': V, 'R': R, 'hb': HB, 'types': TYPES, 'tokens': toks},
          open(P + '/pack/ml/model_meta.json', 'w'))
print('done', flush=True)
