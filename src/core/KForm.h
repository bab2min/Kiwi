#pragma once

#include <kiwi/Types.h>

namespace kiwi
{
	inline bool isWebTag(POSTag t)
	{
		return POSTag::w_url <= t && t <= POSTag::w_hashtag;
	}

	POSTag toPOSTag(const std::u16string& tagStr);
	const char* tagToString(POSTag t);
	const kchar_t* tagToKString(POSTag t);
	struct Form;

	struct Morpheme
	{
#ifdef _DEBUG
		static size_t uid;
		size_t id;
#endif
		const KString* kform = nullptr;
		POSTag tag = POSTag::unknown;
		CondVowel vowel = CondVowel::none;
		CondPolarity polar = CondPolarity::none;
		uint8_t combineSocket = 0;
		std::unique_ptr<std::vector<const Morpheme*>> chunks;
		int32_t combined = 0;
		float userScore = 0;

		Morpheme(const KString& _form = {},
			POSTag _tag = POSTag::unknown,
			CondVowel _vowel = CondVowel::none,
			CondPolarity _polar = CondPolarity::none,
			uint8_t _combineSocket = 0)
			: tag(_tag), vowel(_vowel), polar(_polar), combineSocket(_combineSocket)
#ifdef  _DEBUG
			, id(uid++)
#endif //  _DEBUG
		{
		}

		Morpheme(const Morpheme& m) :
			kform(m.kform), tag(m.tag), vowel(m.vowel), polar(m.polar),
			combineSocket(m.combineSocket), chunks(m.chunks ? new std::vector<const Morpheme*>(*m.chunks) : nullptr),
			combined(m.combined), userScore(m.userScore)
		{
		}

		const KString& getForm() const { return *kform; }
		const Morpheme* getCombined() const { return this + combined; }

		template<class _Istream>
		void readFromBin(_Istream& is, const std::function<const Morpheme*(size_t)>& mapper);
		void writeToBin(std::ostream& os, const std::function<size_t(const Morpheme*)>& mapper) const;

		std::ostream& print(std::ostream& os) const;
	};

	struct Form
	{
		KString form;
		CondVowel vowel = CondVowel::none;
		CondPolarity polar = CondPolarity::none;

		std::vector<const Morpheme*> candidate;
		Form(const kchar_t* _form = nullptr);
		Form(const KString& _form, CondVowel _vowel, CondPolarity _polar) 
			: form(_form), vowel(_vowel), polar(_polar)
		{}

		Form(const Form&) = default;
		Form(Form&&) = default;

		bool operator < (const Form& o) const
		{
			if (form < o.form) return true;
			if (form > o.form) return false;
			if (vowel < o.vowel) return true;
			if (vowel > o.vowel) return false;
			return polar < o.polar;

		}

		Form& operator=(const Form&) = default;
		Form& operator=(Form&&) = default;

		template<class _Istream>
		void readFromBin(_Istream& is, const std::function<const Morpheme*(size_t)>& mapper);
		void writeToBin(std::ostream& os, const std::function<size_t(const Morpheme*)>& mapper) const;
	};

}
