import utils
import math
import pickle
import re

class WordDist:
    def __init__(self):
        self.p = {}
        self.c = 0

    def addWord(self, pos, w):
        self.p[w] = self.p.get(w, 0) + 1/abs(pos)

class DistModelGenerator:
    def __init__(self, maxN):
        self.words = {}
        self.morphs = []
        self.allCount = 0
        self.rgxFilter = re.compile('^[가-힣]+$')
        self.maxN = maxN

    def isValidTag(tag):
        if tag.startswith('S'): return False
        if tag in ('NA', 'NF', 'NV', 'UNA', 'UNT', 'UNC'): return False
        return True

    def isValidForm(self, form):
        return self.rgxFilter.match(form)

    def flush(self):
        for i, mi in enumerate(self.morphs):
            if not DistModelGenerator.isValidTag(mi[1]): continue
            if mi not in self.words: self.words[mi] = WordDist()
            p = 0
            for j, mj in enumerate(self.morphs):
                if not DistModelGenerator.isValidTag(mj[1]): continue
                if i==j or abs(i-j) > self.maxN: continue
                self.words[mi].addWord(i-j, mj)
                p += 1
            if p: self.words[mi].c += 1
        self.morphs = []

    def procLine(self, line):
        sp = line.strip().split('\t')
        if len(sp) < 3:
            self.flush()
            return
        morphs = zip(sp[1::2], sp[2::2])
        self.morphs += morphs

    def recalc(self):
        self.allCount = 0
        for m, d in self.words.items():
            self.allCount += d.c

    def writeToFile(self, file, filt = None):
        for (form, tag) in sorted(self.words):
            d = self.words[form, tag]
            if d.c < 30: continue
            if tag.startswith('E') or tag.startswith('J'): continue
            if not self.isValidForm(form): continue
            if filt and (form + '/' + tag) not in filt: continue
            file.write('%s/%s\t%d\t' % (utils.normalizeHangul(form), tag, d.c))
            pmi = {a:math.log(b / d.c / self.words[a].c * self.allCount) for a, b in d.p.items() if self.words[a].c >= 30}
            for (f, t) in sorted(pmi, key=pmi.get, reverse=True):
                if not self.isValidForm(f): continue
                if abs(pmi[f, t]) < 2: continue
                file.write('%s/%s\t%g\t' % (utils.normalizeHangul(f), t, pmi[f, t]))
            file.write('\n')

maxN = 5
print('Loading...')
try:
    with open('dmg.%d.pickle' % maxN, 'rb') as f:
        mg = pickle.load(f)
        mg.rgxFilter = re.compile('^[가-힣]+$')
except:
    mg = DistModelGenerator(maxN)
    for line in open('ML_lit.txt', encoding='utf-8'):
        mg.procLine(line)
    mg.flush()
    for line in open('ML_spo.txt', encoding='utf-8'):
        mg.procLine(line)
    mg.flush()
    with open('dmg.%d.pickle' % maxN, 'wb') as f:
        pickle.dump(mg, f)
print('Recalc...')
mg.recalc()
print('Writing...')
filt = set()
for line in open('ambiguityWords.txt', encoding='utf-8'):
    filt.add(line.strip())
with open('distModel.txt', 'w', encoding='utf-8') as output:
    mg.writeToFile(output, filt)

