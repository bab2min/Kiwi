import re
import itertools
import utils

class MorphemeModel:
    def __init__(self, filename):
        self.morphemes = {}
        self.index = {}
        self.load(filename)

    def load(self, filename):
        for line in open(filename, encoding='utf-8'):
            c = line.strip().split('\t')
            k = tuple(c[0:2])
            f = tuple(map(float, c[2:7]))
            m = max([(f[1], MorphemeModel.isVowel), (f[2], MorphemeModel.isVocalic), (f[3], MorphemeModel.isVocalicH),
             (1-f[1], MorphemeModel.notVowel), (1-f[2], MorphemeModel.notVocalic), (1-f[3], MorphemeModel.notVocalicH)], key=lambda x:x[0])
            if m[0] > 0.95: cond = [m[1]]
            else: cond = []
            p = {}
            for i in c[8:]:
                x = i.split(':')
                p[x[0]] = float(x[1])
            p2 = {}
            for i,v in p.items():
                p2[i[:1]] = p2.get(i[:1], 0) + v
            p.update(p2)
            self.morphemes[k] = (cond,) + f + (p,)
            if k[1] not in self.index: self.index[k[1]] = [k]
            else: self.index[k[1]].append(k)

    def isVowel(s):
        return 'ㅏ' <= s[-1] <= 'ㅣ'

    def isVocalic(s):
        return 'ㅏ' <= s[-1] <= 'ㅣ' or s[-1] == 'ㄹ'

    def isVocalicH(s):
        return 'ㅏ' <= s[-1] <= 'ㅣ' or s[-1] == 'ㄹ' or s[-1] == 'ㅎ'

    def notVowel(s):
        return not MorphemeModel.isVowel(s)

    def notVocalic(s):
        return not MorphemeModel.isVocalic(s)

    def notVocalicH(s):
        return not MorphemeModel.isVocalicH(s)

    def isPositive(s):
        for c in reversed(s):
            if not ('ㅏ' <= c <= 'ㅣ'): continue
            if c in ['ㅏ', 'ㅑ', 'ㅗ']: return True
            if c != 'ㅡ': return False
        return False

class RuleModel:
    class CombineRules:
        def __init__(self, posA, posB):
            self.posA = posA
            self.posB = posB
            self.rules = []

        def appendRule(self, formA, formB, result, bcond):
            self.rules.append((formA, formB, re.compile(formA + '\t' + formB), result.split(','), bcond))

        def applyRule(self, formA, formB):
            for rule in self.rules:
                res = [rule[2].sub(i, formA + '\t' + formB) for i in rule[3]]
                if res[0].find('\t') < 0: return res
            return None

    def __init__(self, filename):
        self.rules = []
        self.load(filename)

    def load(self, filename):
        for line in open(filename, encoding='utf-8'):
            c = line.strip().split('\t')
            if len(c) < 2: continue
            if len(c) < 3:
                self.rules.append(self.CombineRules(c[0].split(','), c[1].split(',')))
            else:
                self.rules[-1].appendRule(c[0], c[1], c[2], c[3] if len(c) > 3 else None)

    def applyRules(self, formA, posA, formB, posB):
        for rule in self.rules:
            if not (any(map(posA.startswith, rule.posA)) and any(map(posB.startswith, rule.posB))): continue
            res = rule.applyRule(formA, formB)
            if res: return res
        return None

    def getPrecond(self, posA):
        ret = set()
        for rule in self.rules:
            if posA not in rule.posA: continue
            for f in map(lambda x:(x[0].replace('^', ''), x[4]), rule.rules): ret.add(f)
        return ret

class PosModel:
    def __init__(self, filename):
        self.pos = {}
        self.transition = {}
        self.load(filename)

    def load(self, filename, minThreshold = 0.001):
        for line in open(filename, encoding='utf-8'):
            c = line.strip().split('\t')
            if len(c) < 3: continue
            if not c[1]:
                self.pos[c[0]] = float(c[2])
            else:
                if float(c[2]) < minThreshold: continue
                if c[1] == '^': c[1] = '$'
                if c[0] not in self.transition: self.transition[c[0]] = {c[1]:float(c[2])}
                else: self.transition[c[0]][c[1]] = float(c[2])

        s = sum(map(self.pos.get, self.pos))
        for k in self.pos: self.pos[k] /= s

rm = RuleModel('combineRule.txt')
mm = MorphemeModel('model.txt')
pm = PosModel('pos.txt')
cand = {}
todo = {('VV',) : 1, ('VA',) : 1, ('VX',) : 1, ('VCP',) : 1, ('VCN',) : 1, ('XSV',) : 1, ('XSA',) : 1}
for i in range(8):
    todo2 = {}
    for pos, cp in todo.items():
        for d, p in pm.transition.get(pos[-1], {}).items():
            if not d.startswith('E'): continue
            np = cp * p
            if np < 0.00003: continue
            cand[pos + (d,)] = np
            todo2[pos + (d,)] = np
    todo = todo2
    if not todo: break

chain = {}
for k, p in cand.items():
    chain[k[1:]] =chain.get(k[1:], 0) + pm.pos[k[0]] * p

precondList = ['V', 'VV', 'XSV', 'XSA']
precond = {pos:[(i, pos) for i in rm.getPrecond(pos)] for pos in precondList}
precond['V'] += [('ㅇㅣ', 'VCP'), ('ㅇㅏㄴㅣ', 'VCN')]
precond['VA'] = [(utils.normalizeHangul(line.strip()), 'VA') for line in open('hAdj.txt', encoding='utf-8').readlines()]
#print(precond['V'])
emptyPos = set()

def enumForms(tag):
    return mm.index.get(tag) or precond.get(tag)

def mToStr(p):
    if type(p[0]) == tuple: return p[0][0] + '/' + p[1]
    else: return p[0] + '/' + p[1]

output = open('combined.txt', 'w', encoding='utf-8')

for pk in [''] + list(precond):
    for k in chain:
        if pk:
            k = (pk,) + k
        if len(k) < 2: continue
        if k[1:] in emptyPos: continue
        print(k)
        count = 0
        for ps in itertools.product(*map(enumForms, k)):
            if type(ps[0][0]) == tuple:
                bform, bcond = ps[0][0]
            else: bform, bcond = ps[0][0], None
            btag = ps[0][1]
            bform = [bform]
            res = None
            for p in ps[1:]:
                cond = mm.morphemes[p][0]
                nform = []
                for bf in bform:
                    if not all(map(lambda x:x(bf), cond)) or mm.morphemes[p][6].get(btag, 0) < 0.01:
                        res = None
                        break
                    res = rm.applyRules(bf, btag, *p)
                    if not res: break
                    nform += res
                if not nform: break
                bform = nform
                btag = p[1]
            if not nform: continue
            count += 1
            socket = ''
            for i in nform:
                if pk == 'V' and ps[0][1] == 'V':
                    socket = precond['V'].index(ps[0])+1
                output.write(i + '\t' + str('+'.join(map(mToStr, ps))) + '\t' + (bcond or '') + '\t' + (str(socket) or '')+ '\n')
        if not count: emptyPos.add(k)

output.close()