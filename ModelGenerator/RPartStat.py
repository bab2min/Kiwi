from utils import Hangul
from collections import Counter

byTag = {}
for i in ('NN', 'NR', 'NP', 'XR', 'XS', 'XP', 'VA', 'VV', 'VC', 'VX', 'EC', 'EP', 'EF', 'ET', 'SF', 'SN', 'SP', 'JK', 'JX', 'JC', 'IC', 'MA', 'MM', 'UN'):
    byTag[i] = Counter()

def proc(file):
    ws = []
    def flush():
        for t, u in zip(ws[:-1], ws[1:]):
            if t[1][:2] not in byTag: continue
            byTag[t[1][:2]].update(u[0][0])
        ws.clear()

    for line in file:
        sp = line.strip().split('\t')
        if len(sp) <= 3:
            flush()
            continue
        ws += list(map(lambda x, y: (Hangul.normalizeSplitCoda(x), y), sp[1::2], sp[2::2]))


proc(open('ML_spo.txt', encoding='utf-8'))
proc(open('ML_lit.txt', encoding='utf-8'))

with open('RPartStat.txt', 'w', encoding='utf-8') as out:
    for tag, cnt in byTag.items():
        tot = sum(cnt.values())
        for w, c in cnt.most_common():
            if c < 5: break
            out.write("%s\t%s\t%d\t%g\n" % (tag, w, c, c / tot))
