import sys
import six
import functools

def __c(x):
    return six.unichr(x)

def __items(x):
    return six.iteritems(x)

if sys.version_info[0] <= 2:
    import codecs
    def __u(x):
        return codecs.unicode_escape_decode(x)[0]
else:
    def __u(x):
        return x

TYPE_INITIAL = 0x001
TYPE_MEDIAL = 0x010
TYPE_FINAL = 0x100

INITIALS = list(map(__c, [0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139,
                          0x3141, 0x3142, 0x3143, 0x3145, 0x3146, 0x3147,
                          0x3148, 0x3149, 0x314a, 0x314b, 0x314c, 0x314d,
                          0x314e]))

MEDIALS = list(map(__c, [0x314f, 0x3150, 0x3151, 0x3152, 0x3153, 0x3154,
                         0x3155, 0x3156, 0x3157, 0x3158, 0x3159, 0x315a,
                         0x315b, 0x315c, 0x315d, 0x315e, 0x315f, 0x3160,
                         0x3161, 0x3162, 0x3163]))

FINALS = list(map(__c, [0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136,
                        0x3137, 0x3139, 0x313a, 0x313b, 0x313c, 0x313d,
                        0x313e, 0x313f, 0x3140, 0x3141, 0x3142, 0x3144,
                        0x3145, 0x3146, 0x3147, 0x3148, 0x314a, 0x314b,
                        0x314c, 0x314d, 0x314e]))

CHAR_LISTS = {TYPE_INITIAL: INITIALS, TYPE_MEDIAL: MEDIALS, TYPE_FINAL: FINALS}
CHAR_SETS = dict(map(lambda x: (x[0], set(x[1])), __items(CHAR_LISTS)))
CHARSET = functools.reduce(lambda x, y: x.union(y), CHAR_SETS.values(), set())
CHAR_INDICES = dict(
    map(lambda x: (x[0], dict([(c, i) for i, c in enumerate(x[1])])),
        __items(CHAR_LISTS)))


def check_syllable(x):
    return 0xAC00 <= ord(x) <= 0xD7A3


def jamo_type(x):
    t = 0x000

    for type_code, jamo_set in __items(CHAR_SETS):
        if x in jamo_set:
            t |= type_code

    return t


def split_syllable_char(x):
    """
    Splits a given korean character into components.
    :param x: A complete korean character.
    :return: A tuple of basic characters that constitutes the given characters.
    """
    if len(x) != 1:
        raise ValueError("Input string must have exactly one character.")

    if not check_syllable(x):
        raise ValueError(
            "Input string does not contain a valid Korean character.")

    diff = ord(x) - 0xAC00
    _m = diff % 28
    _d = (diff - _m) // 28

    initial_index = _d // 21
    medial_index = _d % 21
    final_index = _m

    if not final_index:
        result = (INITIALS[initial_index], MEDIALS[medial_index])
    else:
        result = (
            INITIALS[initial_index], MEDIALS[medial_index],
            FINALS[final_index - 1])

    return result


def join_jamos_char(initial, medial, final=None):
    """
    Combines jamos to produce a single syllable.
    :param initial: initial jamo.
    :param medial: medial jamo.
    :param final: final jamo.
    :return: A syllable.
    """
    if initial not in CHAR_SETS[TYPE_INITIAL] or medial not in CHAR_SETS[
        TYPE_MEDIAL] or (final and final not in CHAR_SETS[TYPE_FINAL]):
        raise ValueError("Given Jamo characters are not valid.")

    initial_ind = CHAR_INDICES[TYPE_INITIAL][initial]
    medial_ind = CHAR_INDICES[TYPE_MEDIAL][medial]
    final_ind = CHAR_INDICES[TYPE_FINAL][final] + 1 if final else 0

    b = 0xAC00 + 28 * 21 * initial_ind + 28 * medial_ind + final_ind

    result = __c(b)

    assert check_syllable(result)

    return result


def split_syllables(string):
    """
    Splits a sequence of Korean syllables to produce a sequence of jamos.
    Irrelevant characters will be ignored.
    :param string: A unicode string.
    :return: A converted unicode string.
    """
    new_string = ""
    for c in string:
        if not check_syllable(c):
            new_c = c
        else:
            new_c = "".join(split_syllable_char(c))
        new_string += new_c

    return new_string


def join_jamos(string):
    """
    Combines a sequence of jamos to produce a sequence of syllables.
    Irrelevant characters will be ignored.
    :param string: A unicode string.
    :return: A converted unicode string.
    """
    last_t = 0
    queue = []
    new_string = ""

    def flush(queue, n=0):
        new_queue = []

        while len(queue) > n:
            new_queue.append(queue.pop())

        if len(new_queue) == 1:
            result = new_queue[0]
        elif len(new_queue) >= 2:
            try:
                result = join_jamos_char(*new_queue)
            except ValueError:
                # Invalid jamo combination
                result = "".join(new_queue)
        else:
            result = None

        return result

    for c in string:
        if c not in CHARSET:
            if queue:
                new_c = flush(queue) + c
            else:
                new_c = c

            last_t = 0
        else:
            t = jamo_type(c)
            new_c = None

            if t & TYPE_FINAL == TYPE_FINAL:
                if not (last_t == TYPE_MEDIAL):
                    new_c = flush(queue)
            elif t == TYPE_INITIAL:
                new_c = flush(queue)
            elif t == TYPE_MEDIAL:
                if last_t & TYPE_INITIAL == TYPE_INITIAL:
                    new_c = flush(queue, 1)
                else:
                    new_c = flush(queue)

            last_t = t
            queue.insert(0, c)

        if new_c:
            new_string += new_c

    if queue:
        new_string += flush(queue)

    return new_string

class Hangul:
    def normalizeHangul(s):
        s = split_syllables(s)
        for k, v in {'ᆫ': 'ㄴ', 'ᆯ': 'ㄹ', 'ᄆ': 'ㅁ', 'ᄇ': 'ㅂ', 'ᆼ': 'ㅇ'}.items():
            s = s.replace(k, v)
        if len(s) and 'ㅏ' <= s[0] <= 'ㅣ': return 'ㅇ' + s
        return s

    def splitCoda(hs):
        ret = ''
        for h in hs:
            n = ord(h)
            if 0x314F <= n < 0x3164:
                ret += chr((n - 0x314F) * 28 + 0xC544)
            elif 0xAC00 <= n <= 0xD7AF:
                n -= 0xAC00
                j = n % 28
                ret += chr(n - j + 0xAC00)
                if j: ret += chr(j + 0x11A7)
            else:
                ret += h
        return ret

    repTable = {}
    for i, c in enumerate('ㄱㄲㄳㄴㄵㄶㄷㄹㄺㄻㄼㄽㄾㄿㅀㅁㅂㅄㅅㅆㅇㅈㅊㅋㅌㅍㅎ'):
        repTable[c] = chr(i + 0x11A8)

    for k, v in zip('ᄀᄁᄂᄃᄅᄆᄇᄉᄊᄋᄌᄎᄏᄐᄑᄒ', 'ᆨᆩᆫᆮᆯᆷᆸᆺᆻᆼᆽᆾᆿᇀᇁᇂ'):
        repTable[k] = v

    def normalizeSplitCoda(s):
        s = Hangul.splitCoda(s)
        for k, v in Hangul.repTable.items():
            s = s.replace(k, v)
        return s