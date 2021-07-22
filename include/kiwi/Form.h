#pragma once

#include <kiwi/Types.h>
#include <kiwi/FixedVector.hpp>

namespace kiwi
{
	struct Morpheme;

	template<bool baked>
	struct MorphemeT
	{
		typename std::conditional<baked, const KString*, uint32_t>::type kform = 0;
		POSTag tag = POSTag::unknown;
		CondVowel vowel = CondVowel::none;
		CondPolarity polar = CondPolarity::none;
		uint8_t combineSocket = 0;
		typename std::conditional<baked, FixedVector<const Morpheme*>, std::vector<uint32_t>>::type chunks;
		int32_t combined = 0;
		float userScore = 0;

		MorphemeT(const KString& _form = {},
			POSTag _tag = POSTag::unknown,
			CondVowel _vowel = CondVowel::none,
			CondPolarity _polar = CondPolarity::none,
			uint8_t _combineSocket = 0)
			: tag(_tag), vowel(_vowel), polar(_polar), combineSocket(_combineSocket)
		{
		}

		std::ostream& print(std::ostream& os) const;
		
	};

	struct MorphemeRaw : public MorphemeT<false>
	{
		using MorphemeT<false>::MorphemeT;
		void serializerRead(std::istream& istr);
		void serializerWrite(std::ostream& ostr) const;
	};

	struct Morpheme : public MorphemeT<true>
	{
		using MorphemeT<true>::MorphemeT;

		const KString& getForm() const { return *kform; }
		const Morpheme* getCombined() const { return this + combined; }
	};

	template<bool baked>
	struct FormT
	{
		KString form;
		CondVowel vowel = CondVowel::none;
		CondPolarity polar = CondPolarity::none;
		typename std::conditional<baked, FixedVector<const Morpheme*>, Vector<uint32_t>>::type candidate;

		FormT(const kchar_t* _form = nullptr)
		{
			if (_form) form = _form;
		}
		FormT(const KString& _form, CondVowel _vowel, CondPolarity _polar) 
			: form(_form), vowel(_vowel), polar(_polar)
		{}

		FormT(const FormT&) = default;
		FormT(FormT&&) = default;
		FormT& operator=(const FormT&) = default;
		FormT& operator=(FormT&&) = default;

		bool operator < (const FormT& o) const
		{
			if (form < o.form) return true;
			if (form > o.form) return false;
			if (vowel < o.vowel) return true;
			if (vowel > o.vowel) return false;
			return polar < o.polar;
		}

	};

	struct FormRaw : public FormT<false>
	{
		using FormT<false>::FormT;
		void serializerRead(std::istream& istr);
		void serializerWrite(std::ostream& ostr) const;
	};

	struct Form : public FormT<true>
	{
		using FormT<true>::FormT;
	};

	Form bake(const FormRaw& o, const Morpheme* morphBase);
	Morpheme bake(const MorphemeRaw& o, const Morpheme* morphBase, const Form* formBase);
}
