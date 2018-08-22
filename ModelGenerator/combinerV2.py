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
            if not c[1].startswith('E') and not c[1].startswith('J') and not c[1].startswith('VC'): continue
            if c[1].startswith('E') and c[0].startswith('아'): continue
            k = tuple(c[0:2])
            if k in self.morphemes: continue
            f = tuple(map(float, c[2:8]))
            m = max([(f[2], MorphemeModel.isVowel), (f[3], MorphemeModel.isVocalic), (f[4], MorphemeModel.isVocalicH),
             (1-f[2], MorphemeModel.notVowel), (1-f[3], MorphemeModel.notVocalic), (1-f[4], MorphemeModel.notVocalicH)], key=lambda x:x[0])
            if m[0] >= 0.93: cond = [m[1]]
            else: cond = []
            self.morphemes[k] = (cond,) + f
            if k[1] not in self.index: self.index[k[1]] = [k]
            else: self.index[k[1]].append(k)

    def isVowel(s):
        return '가' <= s[-1] <= '히'

    def isVocalic(s):
        return '가' <= s[-1] <= '히' or s[-1] == 'ᆯ'

    def isVocalicH(s):
        return '가' <= s[-1] <= '히' or s[-1] == 'ᆯ' or s[-1] == 'ᇂ'

    def notVowel(s):
        return not MorphemeModel.isVowel(s)

    def notVocalic(s):
        return not MorphemeModel.isVocalic(s)

    def notVocalicH(s):
        return not MorphemeModel.isVocalicH(s)

    def isSingleVowel(s):
        return 'ㅏ' <= s <= 'ㅣ'

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
            if MorphemeModel.isSingleVowel(formA) and MorphemeModel.isSingleVowel(result):
                for i in range(19):
                    l = chr(28*(21*i + ord(formA)-0x314F) + 0xAC00)
                    r = chr(28*(21*i + ord(result)-0x314F) + 0xAC00)
                    self.rules.append((l, formB, re.compile(l + '\t' + formB), [r], bcond))
            else:
                self.rules.append((formA, formB, re.compile(formA + '\t' + formB), result.split(','), bcond))

        def applyRule(self, formA, formB, bcond):
            for rule in self.rules:
                if (bcond and bcond != rule[4]) or ((self.posA == ['VCP'] or self.posA == ['V']) and bcond != rule[4]): continue
                res = [rule[2].sub(i, formA + '\t' + formB) for i in rule[3]]
                if res[0].find('\t') < 0: return res
            return None

    def __init__(self, filename):
        self.rules = []
        self.load(filename)

    def load(self, filename):
        for line in open(filename, encoding='utf-8'):
            if line.startswith('#'): continue
            c = line.strip().split('\t')
            if len(c) < 2: continue
            if len(c) < 3:
                self.rules.append(self.CombineRules(c[0].split(','), c[1].split(',')))
            else:
                self.rules[-1].appendRule(c[0], c[1], c[2], c[3] if len(c) > 3 else None)

    def applyRules(self, formA, posA, formB, posB, bcond):
        for rule in self.rules:
            if not (any(map(posA.startswith, rule.posA)) and any(map(posB.startswith, rule.posB))): continue
            res = rule.applyRule(formA, formB, bcond)
            if res: return res
        return None

    def getPrecond(self, posA):

        def ruleToPrecond(r):
            t = r[0].replace('^', '')
            mgroup = re.match('\(\[(.*)\]\)', t)
            if mgroup: return [(c, r[4]) for c in mgroup.group(1)]
            else: return [(t, r[4])]

        ret = set()
        for rule in self.rules:
            if posA not in rule.posA: continue
            for f in map(ruleToPrecond, rule.rules):
                ret |= set(f)
        return list(sorted(ret, key=lambda x:(x[0], x[1] or '')))

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

rm = RuleModel('combineRuleV2.txt')
mm = MorphemeModel('fullmodelV2.txt')
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
    chain[k[1:]] = chain.get(k[1:], 0) + pm.pos[k[0]] * p

for d, p in pm.transition.get('NP', {}).items():
    if not d.startswith('J'): continue
    chain[(d,)] = p

for d, p in pm.transition.get('NNB', {}).items():
    if not d.startswith('VCP'): continue
    chain[(d,)] = p


def parseList(line):
    ch = line.strip().split('\t')
    if len(ch) < 2:
        return utils.Hangul.normalizeSplitCoda(ch[0])
    return utils.Hangul.normalizeSplitCoda(ch[0]), ch[1]

precondList = ['V', 'A', 'VA', 'VV', 'VX', 'NP', 'NNB']
precond = {pos:[(i, pos) for i in rm.getPrecond(pos)] for pos in precondList}
precond['V'] += [('이', 'VCP'), (('이', '-Coda'), 'VCP'), ('아니', 'VCN')]
precond['XSV'] = [(parseList(line), 'XSV') for line in open('XSV.txt', encoding='utf-8').readlines()]
precond['XSA'] = [(parseList(line), 'XSA') for line in open('XSA.txt', encoding='utf-8').readlines()]
precond['VA'] += [(parseList(line), 'VA') for line in open('VA.txt', encoding='utf-8').readlines()]
precond['VX'] = [(parseList(line), 'VX') for line in open('VX.txt', encoding='utf-8').readlines()]
precond['VV'] += [(parseList(line), 'VV') for line in open('VV.txt', encoding='utf-8').readlines()]
emptyPos = set()

def enumForms(tag):
    return mm.index.get(tag) or precond.get(tag)

def mToStr(p):
    if type(p[0]) == tuple: return p[0][0] + '/' + p[1]
    else: return p[0] + '/' + p[1]

output = open('combinedV2.txt', 'w', encoding='utf-8')
socketMap = {}

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
            bpcond = bcond
            res = None
            for p in ps[1:]:
                cond = mm.morphemes[p][0]
                nform = []
                for bf in bform:
                    if not all(map(lambda x:x(bf), cond)):
                        res = None
                        break
                    res = rm.applyRules(bf, btag, *p, bpcond)
                    if not res: break
                    nform += res
                if not nform: break
                bform = nform
                btag = p[1]
                bpcond = False
            if not nform: continue
            count += 1
            socket = ''
            for i in nform:
                if (pk == 'V' and ps[0][1] == 'V') or (pk == 'A' and ps[0][1] == 'A'):
                    key = ps[0] if type(ps[0][0]) == str else (ps[0][0][0], ps[0][1])
                    if key in socketMap: socket = socketMap[key]
                    else:
                        socket = len(socketMap) + 1
                        socketMap[key] = socket
                output.write(i + '\t' + str('+'.join(map(mToStr, ps))) + '\t' + (bcond or '') + '\t' + (str(socket) or '')+ '\n')
        if not count: emptyPos.add(k)

output.close()