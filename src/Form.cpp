#include <cassert>
#include <kiwi/Utils.h>
#include <kiwi/Form.h>
#include "serializer.hpp"


using namespace std;
using namespace kiwi;

DEFINE_SERIALIZER_OUTSIDE(kiwi::MorphemeRaw, kform, tag, vowel, polar, combineSocket, combined, userScore, chunks);
DEFINE_SERIALIZER_OUTSIDE(kiwi::FormRaw, form, vowel, polar, candidate);

Form kiwi::bake(const FormRaw& o, const Morpheme* morphBase)
{
	Form ret;
	ret.form = o.form;
	ret.vowel = o.vowel;
	ret.polar = o.polar;
	ret.candidate = FixedVector<const Morpheme*>{o.candidate.size()};
	for (size_t i = 0; i < o.candidate.size(); ++i)
	{
		ret.candidate[i] = morphBase + o.candidate[i];
	}
	return ret;
}

Morpheme kiwi::bake(const MorphemeRaw& o, const Morpheme* morphBase, const Form* formBase)
{
	Morpheme ret;
	ret.kform = &formBase[o.kform].form;
	ret.tag = o.tag;
	ret.vowel = o.vowel;
	ret.polar = o.polar;
	ret.combineSocket = o.combineSocket;
	ret.combined = o.combined;
	ret.userScore = o.userScore;
	ret.chunks = FixedVector<const Morpheme*>{ o.chunks.size() };
	for (size_t i = 0; i < o.chunks.size(); ++i)
	{
		ret.chunks[i] = morphBase + o.chunks[i];
	}
	return ret;
}

template<bool bake>
std::ostream & kiwi::MorphemeT<bake>::print(std::ostream & os) const
{
	os << utf16To8(kform ? u16string{ kform->begin(), kform->end() } : u"_");
	os << '/';
	os << tagToString(tag);
	if (combineSocket) os << '+' << (size_t)combineSocket;
	return os;
}
