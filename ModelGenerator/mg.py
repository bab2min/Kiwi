import utils

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
            if i == 0:
                if w not in self.words: self.words[w] = [0] * 6 + [{}]
                self.words[w][5] += 1
            else:
                bw = ws[i - 1]
                if w not in self.words: self.words[w] = [0] * 6 + [{}]
                self.words[w][0] += 1
                self.words[w][5] += 1
                if len(bw[0]):
                    if ModelGenerator.isVowel(bw[0]): self.words[w][1] += 1
                    if ModelGenerator.isVocalic(bw[0]): self.words[w][2] += 1
                    if ModelGenerator.isVocalicH(bw[0]): self.words[w][3] += 1
                    if ModelGenerator.isPositive(bw[0]): self.words[w][4] += 1
                self.words[w][6][bw[1]] = self.words[w][6].get(bw[1], 0) + 1

    def writeForm(self, filename):
        f = open(filename, 'w', encoding='utf-8')
        for k in sorted(self.words):
            d = self.words[k]
            if k[1] in ('NA', 'NF', 'NV'): continue
            if d[5] < 5: continue
            if not all(map(lambda x:'ㄱ'<=x<='ㅣ', k[0])): continue
            if d[5]/self.formDict[k[0]] < 0.00005: continue
            #if not (k[1].startswith('E') or k[1].startswith('J') or k[1].startswith('VC')): continue
            '''if (len(k[0]) < 2 and not ModelGenerator.isVowel(k[0])) or (len(k[0]) >= 2 and not ModelGenerator.isVowel(k[0][0]) and not ModelGenerator.isVowel(k[0][1])) and d[3]/d[0] > 0.5:
                d[1] += d[0] - d[3]
                d[2] += d[0] - d[3]
                d[3] += d[0] - d[3]
                if k[1].startswith('E'):
                    d[1] -= 1
                    d[2] -= 1
                else:
                    d[2] -= 1
                    d[3] -= 1'''
            d[0] += 4
            d[1] += 2
            d[2] += 2
            d[3] += 2
            d[4] += 2
            f.write("%s\t%s\t%g\t%g\t%g\t%g\t%g\t" % (k[0], k[1], d[5]/self.formDict[k[0]], d[1]/d[0], d[2]/d[0], d[3]/d[0], d[4]/d[0]))
            for tag in sorted(d[6], key=d[6].get, reverse=True):
                f.write("%s:%g\t" % (tag, d[6][tag]/d[0]))
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