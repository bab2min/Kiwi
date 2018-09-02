from utils import Hangul
from collections import Counter
import math

if 1:
    forms = ['으로','로','으로써','의','이라','이라고','이','가','을','를','은','는',
            '에','에서','과','와','만','에게','까지','처럼','보다','이나','부터']
    totalCnt = Counter()
    nounCnt = Counter()
    def proc(file):
        for line in file:
            sp = line.strip().split('\t')
            if len(sp) < 3: continue
            for f in forms:
                if sp[0].endswith(f):
                    totalCnt.update([f])
                    break
            ws = list(zip(sp[1::2], sp[2::2]))
            w = ws[-1]
            if not w[1].startswith('NN'): continue
            for f in forms:
                if w[0].endswith(f):
                    nounCnt.update([f])
                    break
    proc(open('ML_spo.txt', encoding='utf-8'))
    proc(open('ML_lit.txt', encoding='utf-8'))

    with open('NounTailList.txt', 'w', encoding='utf-8') as out:
        for f in forms:
            w = math.log(nounCnt.get(f, 0.1) / (totalCnt.get(f, 0.1) - nounCnt.get(f, 0)))
            out.write("%s\t%g\t%d\t%d\n" % (f, w, nounCnt.get(f, 0), totalCnt.get(f, 0) - nounCnt.get(f, 0)))

else:
    byTag = {}
    #lastByTag = {}
    for i in ('NN', 'XR', 'VV'):
        byTag[i, 0] = Counter()
        byTag[i, 1] = Counter()
        byTag[i, 2] = Counter()
        byTag[i, 3] = Counter()
        #lastByTag[i] = Counter()

    def proc(file):
        for line in file:
            sp = line.strip().split('\t')
            if len(sp) <= 3: continue
            ws = list(map(lambda x, y: (x, y), sp[1::2], sp[2::2])) + [('$', 'UN')]
            for t, u in zip(ws[:-1], ws[1:]):
                if not t[0] or not u[0]: continue
                splitted = Hangul.normalizeSplitCoda(t[0])
                coda = 1 if 0x11A8 <= ord(splitted[-1]) <= 0x11C2 else 0
                if (t[1][:2], 0) in byTag:
                    byTag[t[1][:2], coda].update(u[0][0])
                    #lastByTag[t[1][:2]].update(t[0][-1])
                if not t[1].startswith('NN'):
                    byTag['NN', coda + 2].update(u[0][0])
                if not t[1].startswith('XR'):
                    byTag['XR', coda + 2].update(u[0][0])
                if not t[1].startswith('VV'):
                    byTag['VV', coda + 2].update(u[0][0])


    proc(open('ML_spo.txt', encoding='utf-8'))
    proc(open('ML_lit.txt', encoding='utf-8'))

    with open('RPartStat.txt', 'w', encoding='utf-8') as out:
        for tag, cnt in byTag.items():
            tot = sum(cnt.values())
            out.write("%s\t%d\t%d\n" % (*tag, tot))
            for w, c in cnt.most_common():
                if c < 10: break
                out.write("%s\t%d\t%s\t%d\t%g\n" % (*tag, w, c, c / tot))

    with open('RPosModel.txt', 'w', encoding='utf-8') as out:
        for (pos, coda), cnt in byTag.items():
            if coda >= 2: continue
            ncnt = byTag[pos, coda + 2]
            w = {k:math.log(cnt.get(k, .1) / ncnt.get(k, .1)) for k in cnt.keys() | ncnt.keys() if cnt.get(k, 0) + ncnt.get(k, 0) >= 10}
            for k in sorted(w, key=w.get,reverse=True):
                out.write("%s\t%d\t%s\t%g\n" % (pos + ('P' if pos == 'NN' else ''), coda, k, w[k]))


    #with open('LPartTailStat.txt', 'w', encoding='utf-8') as out:
    #    for tag, cnt in lastByTag.items():
    #        tot = sum(cnt.values())
    #        for w, c in cnt.most_common():
    #            if c < 10: break
    #            out.write("%s\t%s\t%d\t%g\n" % (tag, w, c, c / tot))
