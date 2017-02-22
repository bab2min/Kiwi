class ModelGenerator:
    def __init__(self):
        self.words = {}

    def isVowel(s):
        return (ord(s[-1]) - 0xAC00) % 28 == 0

    def isVocalic(s):
        e = (ord(s[-1]) - 0xAC00) % 28
        return e in [0, 8]

    def isVocalicH(s):
        e = (ord(s[-1]) - 0xAC00) % 28
        return e in [0, 8, 27]

    def isPositive(s):
        def vowel(c):
            return int((ord(c) - 0xAC00) / 28) % 21
        for c in reversed(s):
            v = vowel(c)
            if v in [0, 2, 8]: return True
            if v != 18: return False
        return False

    def normalizeHangul(s):
        def split(c):
            from hangul_utils import split_syllable_char
            jm = split_syllable_char(c)
            if jm[0] == 'ㅇ': return jm[1:]
            return jm

        def split_syllables(string):
            from hangul_utils import check_syllable
            new_string = ""
            for c in string:
                if not check_syllable(c):
                    new_c = c
                else:
                    new_c = "".join(split(c))
                new_string += new_c
            return new_string

        s = split_syllables(s)
        for k, v in {'ᆫ':'ㄴ','ᆯ':'ㄹ','ᄆ':'ㅁ','ᄇ':'ㅂ','ᆼ':'ㅇ'}.items():
            s = s.replace(k, v)

        return s

    def procLine(self, str):
        sp = line.strip().split('\t')
        if len(sp) <= 3: return
        ws = list(map(lambda x, y: (x, y), sp[1::2], sp[2::2]))
        for i, w in enumerate(ws):
            if i == 0: continue
            bw = ws[i - 1]
            if w not in self.words: self.words[w] = [0] * 5 + [{}]
            self.words[w][0] += 1
            if len(bw[0]) and 0xAC00 <= ord(bw[0][0]) < 0xD800:
                if ModelGenerator.isVowel(bw[0]): self.words[w][1] += 1
                if ModelGenerator.isVocalic(bw[0]): self.words[w][2] += 1
                if ModelGenerator.isVocalicH(bw[0]): self.words[w][3] += 1
                if ModelGenerator.isPositive(bw[0]): self.words[w][4] += 1
            self.words[w][5][bw[1]] = self.words[w][5].get(bw[1], 0) + 1

    def writeToFile(self, filename):
        f = open(filename, 'w', encoding='utf-8')
        for k in sorted(self.words, key=lambda x:self.words.get(x)[0], reverse=True):
            d = self.words[k]
            if d[0] < 10: continue
            if k[1].startswith('N') or k[1].startswith('S'): continue
            f.write("%s\t%s\t%d\t%g\t%g\t%g\t%g\t" % (ModelGenerator.normalizeHangul(k[0]), k[1], d[0], d[1]/d[0], d[2]/d[0], d[3]/d[0], d[4]/d[0]))
            for tag in sorted(d[5], key=d[5].get, reverse=True):
                f.write("%s:%g\t" % (tag, d[5][tag]/d[0]))
            f.write('\n')
        f.close()

mg = ModelGenerator()
for line in open('ML_lit.txt', encoding='utf-8'):
    mg.procLine(line)
for line in open('ML_spo.txt', encoding='utf-8'):
    mg.procLine(line)

mg.writeToFile('model.txt')