import re
from collections import Counter, defaultdict

def test_coda_vowel(s):
    s = ord(s[-1])
    if s < 0xAC00 or s > 0xD7A3: return None
    coda = (s - 0xAC00) % 28
    if coda == 0: 
        return 3
    if coda == 8: 
        return 2
    if coda == 27:
        return 1
    return 0

def test_irregular_cand(s):
    s = ord(s[-1])
    if s < 0xAC00 or s > 0xD7A3: return False
    coda = (s - 0xAC00) % 28
    return coda in (7, 17, 19)

class MorphemeExtractor:

    stop_pattern = re.compile(r'<.*>|name[0-9]*|[.,?!:;]|.*[()].*')
    irregular_cands = re.compile(r'V[VAX]')
    consonant_onset = re.compile(r'[가-싷자-힣]')

    def __init__(self):
        self._morpheme_cnt = Counter()
        self._josa_cnts = defaultdict(Counter)
        self._regular_cnt = Counter()
        self._irregular_cnt = Counter()
    
    def feed(self, form, morphemes):
        self._morpheme_cnt.update(((f, t) for f, t in morphemes if not t.startswith('S')))
        prev_form = None
        prev_tag = None
        for f, t in morphemes:
            if prev_form:
                if self.irregular_cands.match(prev_tag) and not self.consonant_onset.match(f) and test_irregular_cand(prev_form):
                    if prev_form in form:
                        self._regular_cnt[prev_form, prev_tag] += 1
                    else:
                        self._irregular_cnt[prev_form, prev_tag] += 1
                if t.startswith('J'):
                    v = test_coda_vowel(prev_form)
                    if v is not None:
                        self._josa_cnts[f, t][v] += 1
            prev_form = f
            prev_tag = t
    
    def save(self, path, min_cnt=5):
        with open(path, 'w', encoding='utf-8') as fout:
            for (form, tag), cnt in self._morpheme_cnt.most_common():
                if cnt < min_cnt: break
                if self.stop_pattern.fullmatch(form): continue
                if tag.startswith('J'):
                    left_dists = self._josa_cnts[form, tag].copy()
                    for i in range(4):
                        left_dists[i] += 3
                    tot = sum(left_dists.values())
                    dists = {}
                    dists['vowel'] = left_dists[3] / tot
                    #dists['vocalic'] = (left_dists[3] + left_dists[2]) / tot
                    #dists['vocalic_h'] = (left_dists[3] + left_dists[2] + left_dists[1]) / tot
                    dists['non_vowel'] = 1 - dists['vowel']
                    #dists['non_vocalic'] = 1 - dists['vocalic']
                    #dists['non_vocalic_h'] = 1 - dists['vocalic_h']
                    v, k = max((v, k) for k, v in dists.items())
                    if v >= 0.85:
                        print(form, tag, cnt, k, v, sep='\t', file=fout)
                    else:
                        print(form, tag, cnt, sep='\t', file=fout)
                else:
                    regs = self._regular_cnt[form, tag]
                    irregs = self._irregular_cnt[form, tag]
                    if regs or irregs:
                        t = regs + irregs
                        if regs / t >= 0.1:
                            print(form, tag, cnt, 'reg', sep='\t', file=fout)
                        if irregs / t >= 0.1:
                            print(form, tag, cnt, 'irreg', sep='\t', file=fout)
                    else:
                        print(form, tag, cnt, sep='\t', file=fout)


def load_kiwi_corpus(file):
    for line in open(file, encoding='utf-8'):
        fd = line.rstrip().split('\t')
        if len(fd) <= 1: continue
        yield fd[0], list(zip(fd[1::2], fd[2::2]))

def main(args):
    me = MorphemeExtractor()
    for f in args.input:
        for form, morphs in load_kiwi_corpus(f):
            me.feed(form, morphs)
    
    me.save(args.output, args.min_cnt)

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('input', nargs='+')
    parser.add_argument('output')
    parser.add_argument('--min_cnt', default=5, type=int)
    main(parser.parse_args())
