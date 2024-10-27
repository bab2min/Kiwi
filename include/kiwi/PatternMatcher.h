#pragma once

#include <vector>
#include <string>
#include <memory>
#include "Types.h"

namespace kiwi
{
	enum class Match : size_t
	{
		none = 0,
		url = 1 << 0, /**< 인터넷 주소 URL 형태의 텍스트를 w_url 태그에 매칭한다 */
		email = 1 << 1, /**< 이메일 주소 형태의 텍스트를 w_email 태그에 매칭한다 */
		hashtag = 1 << 2, /**< 해시태그 형태의 텍스트(#해시)를 w_hashtag 태그에 매칭한다 */
		mention = 1 << 3, /**< 멘션 형태의 텍스트(@멘션)를 w_mention 태그에 매칭한다 */
		serial = 1 << 4, /**< 일련 번호 형태의 텍스트를 w_serial 태그에 매칭한다 */
		emoji = 1 << 5, /**< 이모지 문자를 w_emoji 태그에 매칭한다 */
		normalizeCoda = 1 << 16, /**< 초성체가 앞 어절의 받침에 따라붙은 경우를 정규화하여 매칭한다 */
		joinNounPrefix = 1 << 17, /**< 체언접두사(XPN)를 분리하지 않고 합쳐서 매칭한다 */
		joinNounSuffix = 1 << 18, /**< 명사파생접미사(XSN)를 분리하지 않고 합쳐서 매칭한다 */
		joinVerbSuffix = 1 << 19, /**< 동사파생접미사(XSV)를 분리하지 않고 합쳐서 매칭한다 */
		joinAdjSuffix = 1 << 20, /**< 형용사파생접미사(XSA)를 분리하지 않고 합쳐서 매칭한다 */
		joinAdvSuffix = 1 << 21, /**< 부사파생접미사(XSM)를 분리하지 않고 합쳐서 매칭한다 */
		splitComplex = 1 << 22, /**< 더 작은 단위로 분할될 수 있는 형태소는 더 분할하여 매칭한다 */
		zCoda = 1 << 23, /**< 어미 및 조사에 덧붙은 받침이 있는 경우 이를 분리하여 z_coda 태그로 매칭한다 */
		compatibleJamo = 1 << 24, /**< 출력시 한글 첫가끝 자모를 호환가능한 자모로 변환한다. */
		splitSaisiot = 1 << 25, /**< 사이시옷이 포함된 합성명사를 분리하여 매칭한다. */
		mergeSaisiot = 1 << 26, /**< 사이시옷이 포함된 것으로 추정되는 명사를 결합하여 매칭한다. */
		joinVSuffix = joinVerbSuffix | joinAdjSuffix,
		joinAffix = joinNounPrefix | joinNounSuffix | joinVerbSuffix | joinAdjSuffix | joinAdvSuffix,
		all = url | email | hashtag | mention | serial | emoji | zCoda,
		allWithNormalizing = all | normalizeCoda,
	};

	std::pair<size_t, kiwi::POSTag> matchPattern(char16_t left, const char16_t* first, const char16_t* last, Match matchOptions);
}

KIWI_DEFINE_ENUM_FLAG_OPERATORS(kiwi::Match);
