import utils
import math

class PrecombinedModelGenerator:
    def __init__(self, filename):
        self.vCond = {}
        self.sufSet = set()
        for line in open(filename, encoding='utf-8'):
            ch = line.strip().split('\t')
            if len(ch) < 2: continue
            v = ch[1].split('+')[0]
            if not v.endswith('/V') and not v.endswith('/A'): continue
            vc = v.split('/')[0], 'V' + v.split('/')[1], (ch[2] if ch[2].find('Positive') >= 0 else '')
            self.sufSet.add(vc[0])
            if vc in self.vCond: self.vCond[vc][0].add(ch[0][0])
            else: self.vCond[vc] = set([ch[0][0]]), ch[3]

    @staticmethod
    def isPositive(s):
        for c in reversed(s):
            if not ('ㅏ' <= c <= 'ㅣ'): continue
            if c in ['ㅏ', 'ㅑ', 'ㅗ']: return True
            if c != 'ㅡ': return False
        return False

    def findVCond(self, suf, stem, pos):
        positive = ('+' if PrecombinedModelGenerator.isPositive(stem) else '-') + 'Positive'
        return self.vCond.get((suf, pos, positive)) or self.vCond.get((suf, pos, '')) or self.vCond.get((suf, 'VV', positive)) or self.vCond.get((suf, 'VV', ''))

    def extractPrecombinedFromModel(self, filename):
        ret = []
        for line in open(filename, encoding='utf-8'):
            ch = line.strip().split('\t')
            if len(ch) < 3: continue
            if not ch[1].startswith('V') and ch[1] not in ('XSV', 'XSA'): continue
            if ch[1].startswith('VC'): continue
            if ch[0][-2:] in self.sufSet and len(ch[0]) >= 2:
                suf = ch[0][-2:]
                stem = ch[0][:-2]
            elif ch[0][-1] in self.sufSet and len(ch[0]) >= 1:
                suf = ch[0][-1:]
                stem = ch[0][:-1]
            else: continue
            if not stem: continue
            ch[0] = stem + '+' + suf
            vCond = self.findVCond(suf, stem, ch[1])
            if not vCond: continue
            ch[2:] = [''.join(sorted(vCond[0])), str(vCond[1])]
            ret.append(ch)
        return ret

pmg = PrecombinedModelGenerator('combinedV2.txt')
with open('precombinedV2.txt', 'w', encoding='utf-8') as output:
    for t in pmg.extractPrecombinedFromModel('fullmodelV2.txt'):
        output.write('\t'.join(t) + '\n')
