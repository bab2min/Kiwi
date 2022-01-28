#include <cassert>
#include <kiwi/Utils.h>
#include <kiwi/Form.h>
#include "serializer.hpp"

using namespace std;

namespace kiwi
{
	MorphemeRaw::MorphemeRaw() = default;

	MorphemeRaw::~MorphemeRaw() = default;

	MorphemeRaw::MorphemeRaw(const MorphemeRaw&) = default;

	MorphemeRaw::MorphemeRaw(MorphemeRaw&&) = default;

	MorphemeRaw& MorphemeRaw::operator=(const MorphemeRaw&) = default;

	MorphemeRaw& MorphemeRaw::operator=(MorphemeRaw&&) = default;

	MorphemeRaw::MorphemeRaw(
		POSTag _tag,
		CondVowel _vowel,
		CondPolarity _polar,
		uint8_t _combineSocket)
		: tag(_tag), vowel(_vowel), polar(_polar), combineSocket(_combineSocket)
	{
	}

	DEFINE_SERIALIZER_OUTSIDE(MorphemeRaw, kform, tag, vowel, polar, combineSocket, combined, userScore, chunks);

	Morpheme::Morpheme() = default;

	Morpheme::~Morpheme() = default;

	Morpheme::Morpheme(const Morpheme&) = default;

	Morpheme::Morpheme(Morpheme&&) = default;

	Morpheme& Morpheme::operator=(const Morpheme&) = default;

	Morpheme& Morpheme::operator=(Morpheme&&) = default;

	FormRaw::FormRaw() = default;

	FormRaw::~FormRaw() = default;

	FormRaw::FormRaw(const FormRaw&) = default;

	FormRaw::FormRaw(FormRaw&&) = default;

	FormRaw& FormRaw::operator=(const FormRaw&) = default;

	FormRaw& FormRaw::operator=(FormRaw&&) = default;

	FormRaw::FormRaw(const KString& _form, CondVowel _vowel, CondPolarity _polar)
		: form(_form), vowel(_vowel), polar(_polar)
	{}

	bool FormRaw::operator<(const FormRaw& o) const
	{
		if (form < o.form) return true;
		if (form > o.form) return false;
		if (vowel < o.vowel) return true;
		if (vowel > o.vowel) return false;
		return polar < o.polar;
	}

	DEFINE_SERIALIZER_OUTSIDE(FormRaw, form, vowel, polar, candidate);

	Form::Form() = default;

	Form::~Form() = default;

	Form::Form(const Form&) = default;

	Form::Form(Form&&) = default;

	Form& Form::operator=(const Form&) = default;

	Form& Form::operator=(Form&&) = default;

	Form bake(const FormRaw& o, const Morpheme* morphBase)
	{
		Form ret;
		ret.form = o.form;
		ret.vowel = o.vowel;
		ret.polar = o.polar;
		ret.candidate = FixedVector<const Morpheme*>{ o.candidate.size() };
		for (size_t i = 0; i < o.candidate.size(); ++i)
		{
			ret.candidate[i] = morphBase + o.candidate[i];
		}
		return ret;
	}

	Morpheme bake(const MorphemeRaw& o, const Morpheme* morphBase, const Form* formBase)
	{
		Morpheme ret;
		ret.kform = &formBase[o.kform].form;
		ret.tag = o.tag;
		ret.vowel = o.vowel;
		ret.polar = o.polar;
		ret.combineSocket = o.combineSocket;
		ret.combined = o.combined;
		ret.userScore = o.userScore;
		ret.lmMorphemeId = o.lmMorphemeId;
		ret.chunks = FixedVector<const Morpheme*>{ o.chunks.size() };
		for (size_t i = 0; i < o.chunks.size(); ++i)
		{
			ret.chunks[i] = morphBase + o.chunks[i];
		}
		return ret;
	}

	std::ostream& Morpheme::print(std::ostream& os) const
	{
		os << utf16To8(kform ? u16string{ kform->begin(), kform->end() } : u"_");
		os << '/';
		os << tagToString(tag);
		if (combineSocket) os << '+' << (size_t)combineSocket;
		return os;
	}
}