def normalizeHangul(s):
    def split(c):
        from hangul_utils import split_syllable_char
        jm = split_syllable_char(c)
        #if jm[0] == 'ㅇ': return jm[1:]
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
    for k, v in {'ᆫ': 'ㄴ', 'ᆯ': 'ㄹ', 'ᄆ': 'ㅁ', 'ᄇ': 'ㅂ', 'ᆼ': 'ㅇ'}.items():
        s = s.replace(k, v)
    if len(s) and 'ㅏ' <= s[0] <= 'ㅣ': return 'ㅇ' + s
    return s