import utils

class WordInfo:
    def __init__(self):
        self.totalCount = 0
        self.postCount = 0
        self.vowelCount = 0
        self.vocalicCount = 0
        self.vocalicHCount = 0
        self.positiveCount = 0
        self.prePos = {}
        self.regularityCount = 0
        self.rTotalCount = 0


class ModelGenerator:
    def __init__(self):
        self.words = {}
        self.pos = {}
        self.biPos = {}
        self.formDict = {}

    def isVowel(s):
        return 'ㅏ' <= s[-1] <= 'ㅣ'

    def isVocalic(s):
        return 'ㅏ' <= s[-1] <= 'ㅣ' or s[-1] == 'ㄹ'

    def isVocalicH(s):
        return 'ㅏ' <= s[-1] <= 'ㅣ' or s[-1] == 'ㄹ' or s[-1] == 'ㅎ'

    def isPositive(s):
        for c in reversed(s):
            if not ('ㅏ' <= c <= 'ㅣ'): continue
            if c in ['ㅏ', 'ㅑ', 'ㅗ']: return True
            if c != 'ㅡ': return False
        return False

    def regularity(uform, oform):
        if oform in uform: return True
        if oform[-1] == 'ㄷ' and oform[:-1] + 'ㄹ' in uform: return False
        if oform[-1] == 'ㅅ' and oform[:-1] + 'ㅇ' in uform: return False
        if oform[-1] == 'ㅂ' and oform[:-1] + 'ㅇ' in uform: return False
        return None

    def procLine(self, str):
        sp = line.strip().split('\t')
        if len(sp) <= 3: return
        ws = list(map(lambda x, y: (utils.normalizeHangul(x), y), sp[1::2], sp[2::2]))

        self.pos['^'] = self.pos.get('^', 0) + 1
        self.biPos['^', ws[0][1]] = self.biPos.get(('^', ws[0][1]), 0) + 1
        for i, w in enumerate(ws):
            self.pos[w[1]] = self.pos.get(w[1], 0) + 1
            bg = (w[1], ws[i+1][1] if i+1 < len(ws) else '^')
            self.biPos[bg] = self.biPos.get(bg, 0) + 1
            self.formDict[w[0]] = self.formDict.get(w[0], 0) + 1
            if w not in self.words: self.words[w] = WordInfo()
            self.words[w].totalCount += 1
            if w[1].startswith('V') and w[0][-1] in ['ㄷ', 'ㅅ', 'ㅂ']:
                rg = ModelGenerator.regularity(utils.normalizeHangul(sp[0]), w[0])
                if rg != None and rg: self.words[w].regularityCount += 1
                if rg != None: self.words[w].rTotalCount += 1
            if i == 0: continue
            bw = ws[i - 1]
            self.words[w].postCount += 1
            if len(bw[0]):
                if ModelGenerator.isVowel(bw[0]): self.words[w].vowelCount += 1
                if ModelGenerator.isVocalic(bw[0]): self.words[w].vocalicCount += 1
                if ModelGenerator.isVocalicH(bw[0]): self.words[w].vocalicHCount += 1
                if ModelGenerator.isPositive(bw[0]): self.words[w].positiveCount += 1
            self.words[w].prePos[bw[1]] = self.words[w].prePos.get(bw[1], 0) + 1

    def writeForm(self, filename):
        f = open(filename, 'w', encoding='utf-8')
        for k in sorted(self.words):
            d = self.words[k]
            if k[1] in ('NA', 'NF', 'NV', 'UNA', 'UNT'): continue
            if d.totalCount < 5: continue
            if not all(map(lambda x:'ㄱ'<=x<='ㅣ', k[0])): continue
            if d.totalCount/self.formDict[k[0]] < 0.00005: continue
            # 빈도가 적은 단어가 극단적인 값을 갖지 않도록 기본수치를 늘려줌
            d.postCount += 20
            d.vowelCount += 10
            d.vocalicCount += 10
            d.vocalicHCount += 10
            d.positiveCount += 10
            # 조사의 경우 극성이 없으므로 극성 수치를 50%에 맞춘다.
            if k[1].startswith('J'):
                d.positiveCount = int(d.postCount / 2)
            f.write("%s\t%s\t%d\t%g\t%g\t%g\t%g\t%g\t%g\t" % (k[0], k[1], d.totalCount, d.totalCount/self.formDict[k[0]], (d.regularityCount+1) / (d.rTotalCount+1), d.vowelCount/d.postCount, d.vocalicCount/d.postCount, d.vocalicHCount/d.postCount, d.positiveCount/d.postCount))
            for tag in sorted(d.prePos, key=d.prePos.get, reverse=True):
                f.write("%s:%g\t" % (tag, d.prePos[tag]/d.postCount))
            f.write('\n')
        f.close()

    def writePos(self, filename):
        f = open(filename, 'w', encoding='utf-8')
        for a, n in self.pos.items():
            if n <= 10: continue
            f.write("%s\t\t%d\n" % (a, n))
        for (a, b), n in self.biPos.items():
            if n <= 10: continue
            f.write("%s\t%s\t%g\n" % (a, b, n/self.pos[a]))
        f.close()


mg = ModelGenerator()
for line in open('ML_lit.txt', encoding='utf-8'):
    mg.procLine(line)
for line in open('ML_spo.txt', encoding='utf-8'):
    mg.procLine(line)

mg.writeForm('fullmodel.txt')
#mg.writePos('pos.txt')