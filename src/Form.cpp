﻿#include <cassert>
#include <kiwi/Utils.h>
#include <kiwi/Form.h>
#include "serializer.hpp"

using namespace std;

namespace kiwi
{
	MorphemeRaw::MorphemeRaw() = default;

	MorphemeRaw::~MorphemeRaw() = default;

	MorphemeRaw::MorphemeRaw(const MorphemeRaw&) = default;

	MorphemeRaw::MorphemeRaw(MorphemeRaw&&) noexcept = default;

	MorphemeRaw& MorphemeRaw::operator=(const MorphemeRaw&) = default;

	MorphemeRaw& MorphemeRaw::operator=(MorphemeRaw&&) = default;

	MorphemeRaw::MorphemeRaw(
		POSTag _tag,
		CondVowel _vowel,
		CondPolarity _polar,
		bool _complex,
		uint8_t _combineSocket)
		: tag(_tag), combineSocket(_combineSocket)
	{
		setVowel(_vowel);
		setPolar(_polar);
		setComplex(_complex);
	}

	DEFINE_SERIALIZER_OUTSIDE(MorphemeRaw, kform, tag, vpPack, senseId, combineSocket, combined, userScore, chunks, chunkPositions, lmMorphemeId, groupId);

	Morpheme::Morpheme() = default;

	Morpheme::~Morpheme() = default;

	Morpheme::Morpheme(const Morpheme&) = default;

	Morpheme::Morpheme(Morpheme&&) noexcept = default;

	Morpheme& Morpheme::operator=(const Morpheme&) = default;

	Morpheme& Morpheme::operator=(Morpheme&&) = default;

	FormRaw::FormRaw() = default;

	FormRaw::~FormRaw() = default;

	FormRaw::FormRaw(const FormRaw&) = default;

	FormRaw::FormRaw(FormRaw&&) noexcept = default;

	FormRaw& FormRaw::operator=(const FormRaw&) = default;

	FormRaw& FormRaw::operator=(FormRaw&&) = default;

	FormRaw::FormRaw(const KString& _form)
		: form(_form)
	{}

	bool FormRaw::operator<(const FormRaw& o) const
	{
		return form < o.form;
	}

	DEFINE_SERIALIZER_OUTSIDE(FormRaw, form, candidate);

	Form::Form() = default;

	Form::~Form() = default;

	Form::Form(const Form&) = default;

	Form::Form(Form&&) noexcept = default;

	Form& Form::operator=(const Form&) = default;

	Form& Form::operator=(Form&&) = default;

	Form bake(const FormRaw& o, const Morpheme* morphBase, bool zCodaAppendable, const Vector<uint32_t>& additionalCands)
	{
		Form ret;
		ret.form = o.form;
		ret.candidate = FixedVector<const Morpheme*>{ o.candidate.size() + additionalCands.size()};
		for (size_t i = 0; i < o.candidate.size(); ++i)
		{
			ret.candidate[i] = morphBase + o.candidate[i];
		}
		for (size_t i = 0; i < additionalCands.size(); ++i)
		{
			ret.candidate[i + o.candidate.size()] = morphBase + additionalCands[i];
		}
		ret.zCodaAppendable = zCodaAppendable ? 1 : 0;
		return ret;
	}

	Morpheme bake(const MorphemeRaw& o, const Morpheme* morphBase, const Form* formBase, const Vector<size_t>& formMap)
	{
		Morpheme ret;
		ret.kform = &formBase[formMap[o.kform]].form;
		ret.tag = o.tag;
		ret.vowel = o.vowel();
		ret.polar = o.polar();
		ret.complex = o.complex();
		ret.combineSocket = o.combineSocket;
		ret.combined = o.combined;
		ret.userScore = o.userScore;
		ret.lmMorphemeId = o.lmMorphemeId;
		ret.senseId = o.senseId;
		ret.chunks = FixedPairVector<const Morpheme*, std::pair<uint8_t, uint8_t>>{ o.chunks.size() };
		for (size_t i = 0; i < o.chunks.size(); ++i)
		{
			ret.chunks[i] = morphBase + o.chunks[i];
			ret.chunks.getSecond(i) = o.chunkPositions[i];
		}
		return ret;
	}

	std::ostream& Morpheme::print(std::ostream& os) const
	{
		os << utf16To8(kform ? joinHangul(*kform) : u"_");
		os << '/';
		os << tagToString(tag);
		if (combineSocket) os << '+' << (size_t)combineSocket;
		return os;
	}
}
