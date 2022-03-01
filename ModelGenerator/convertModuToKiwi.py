import sys
import re
import json

coda_pat = re.compile(r'^(?:(ㄴ)|(ㄹ)|(ㅁ)|(ㅂ))')

def _sub(m):
    return ' ᆫᆯᆷᆸ'[m.lastindex]

def normalize_morpheme(form, tag):
    if tag.startswith('MM'):
        return form, 'MM'
    if tag.startswith('J') or tag.startswith('E'):
        return coda_pat.sub(_sub, form), tag
    return form, tag

def convert(input_file, output_file):
    obj = json.load(input_file)
    for doc in obj['document']:
        for sent in doc['sentence']:
            for word_id, word in enumerate(sent['word'] or [], start=1):
                print(word['form'], end='\t', file=output_file)
                s = []
                for morph in sent['morpheme']:
                    if morph['word_id'] != word_id: continue
                    s.extend(normalize_morpheme(morph['form'], morph['label']))
                print(*s, sep='\t', end='\n', file=output_file)
            print(file=output_file, flush=True)

def main(args):
    if args.output:
        output = open(args.output, 'w', encoding='utf-8')
    else:
        output = sys.stdout
    
    for input in args.input:
        convert(open(input, encoding='utf-8'), output)
    
    if args.output:
        output.close()

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('input', nargs='+')
    parser.add_argument('--output')

    main(parser.parse_args())