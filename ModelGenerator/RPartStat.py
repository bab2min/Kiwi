from utils import Hangul
from collections import Counter

byTag = {}
for i in ('NN', 'NR', 'NP', 'XR', 'XS', 'XP', 'VA', 'VV', 'VC', 'VX', 'EC', 'EP', 'EF', 'ET', 'SF', 'SN', 'SP', 'JK', 'JX', 'JC', 'IC', 'MA', 'MM', 'UN'):
    byTag[i, 0] = Counter()
    byTag[i, 1] = Counter()

def proc(file):
    ws = []
    def flush():
        for t, u in zip(ws[:-1], ws[1:]):
            if (t[1][:2], 0) not in byTag: continue
            splitted = Hangul.normalizeSplitCoda(t[0])
            coda = 1 if 0x11A8 <= ord(splitted[-1]) <= 0x11C2 else 0
            byTag[t[1][:2], coda].update(u[0][0])
        ws.clear()

    for line in file:
        sp = line.strip().split('\t')
        if len(sp) <= 3:
            flush()
            continue
        ws += list(map(lambda x, y: (x, y), sp[1::2], sp[2::2]))


proc(open('ML_spo.txt', encoding='utf-8'))
proc(open('ML_lit.txt', encoding='utf-8'))

with open('RPartStat.txt', 'w', encoding='utf-8') as out:
    for tag, cnt in byTag.items():
        tot = sum(cnt.values())
        for w, c in cnt.most_common():
            if c < 10: break
            out.write("%s\t%d\t%s\t%d\t%g\n" % (*tag, w, c, c / tot))
