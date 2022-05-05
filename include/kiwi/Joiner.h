#pragma once
#include "Types.h"

namespace kiwi
{
	class Kiwi;

	namespace cmb
	{
		class Joiner
		{
			friend class CompiledRule;
			const CompiledRule* cr = nullptr;
			KString stack;
			size_t activeStart = 0;
			POSTag lastTag = POSTag::unknown;
		protected:
			explicit Joiner(const CompiledRule& _cr);
			
			void add(U16StringView form, POSTag tag);

		public:
			~Joiner();

			Joiner(const Joiner&);
			Joiner(Joiner&&);
			Joiner& operator=(const Joiner&);
			Joiner& operator=(Joiner&&);

			void add(const std::u16string& form, POSTag tag);
			void add(const char16_t* form, POSTag tag);

			std::u16string getU16() const;
			std::string getU8() const;
		};

		class AutoJoiner : public Joiner
		{
			friend class kiwi::Kiwi;
			const Kiwi* kiwi = nullptr;

			explicit AutoJoiner(const Kiwi& kiwi);

			void add(U16StringView form, POSTag tag, bool inferRegularity);
		public:
			~AutoJoiner();
			AutoJoiner(const AutoJoiner&);
			AutoJoiner(AutoJoiner&&);
			AutoJoiner& operator=(const AutoJoiner&);
			AutoJoiner& operator=(AutoJoiner&&);

			void add(const std::u16string& form, POSTag tag, bool inferRegularity = true);
			void add(const char16_t* form, POSTag tag, bool inferRegularity = true);
		};
	}
}
