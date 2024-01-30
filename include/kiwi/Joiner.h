#pragma once
#include "Types.h"
#include "ArchUtils.h"
#include "LmState.h"

namespace kiwi
{
	class Kiwi;
	template<ArchType arch> class VoidState;
	struct Form;

	namespace cmb
	{
		class CompiledRule;
		class AutoJoiner;

		enum class Space
		{
			none = 0,
			no_space = 1,
			insert_space = 2,
		};

		class Joiner
		{
			friend class CompiledRule;
			friend class AutoJoiner;
			template<class LmState> friend struct Candidate;
			const CompiledRule* cr = nullptr;
			KString stack;
			std::vector<std::pair<uint32_t, uint32_t>> ranges;
			size_t activeStart = 0;
			POSTag lastTag = POSTag::unknown, anteLastTag = POSTag::unknown;

			explicit Joiner(const CompiledRule& _cr);			
			void add(U16StringView form, POSTag tag, Space space);

		public:
			~Joiner();

			Joiner(const Joiner&);
			Joiner(Joiner&&) noexcept;
			Joiner& operator=(const Joiner&);
			Joiner& operator=(Joiner&&);

			void add(const std::u16string& form, POSTag tag, Space space = Space::none);
			void add(const char16_t* form, POSTag tag, Space space = Space::none);

			std::u16string getU16(std::vector<std::pair<uint32_t, uint32_t>>* rangesOut = nullptr) const;
			std::string getU8(std::vector<std::pair<uint32_t, uint32_t>>* rangesOut = nullptr) const;
		};

		template<class LmState>
		struct Candidate
		{
			Joiner joiner;
			LmState lmState;
			float score = 0;

			Candidate(const CompiledRule& _cr, const LangModel& lm)
				: joiner{ _cr }, lmState{ lm }
			{
			}
		};

		template<ArchType arch>
		struct Candidate<VoidState<arch>>
		{
			Joiner joiner;

			Candidate(const CompiledRule& _cr, const LangModel& lm)
				: joiner{ _cr }
			{
			}
		};

		class AutoJoiner
		{
			friend class kiwi::Kiwi;

			struct AddVisitor;
			struct AddVisitor2;
			const Kiwi* kiwi = nullptr;
			union
			{
				typename std::aligned_storage<sizeof(Vector<char>) + sizeof(int), alignof(Vector<char>)>::type candBuf;
			};

			template<class LmState>
			explicit AutoJoiner(const Kiwi& kiwi, Candidate<LmState>&& state);

			template<class Func>
			void foreachMorpheme(const Form* formHead, Func&& func) const;

			template<class LmState>
			void add(size_t morphemeId, Space space, Vector<Candidate<LmState>>& candidates);

			template<class LmState>
			void add(U16StringView form, POSTag tag, bool inferRegularity, Space space, Vector<Candidate<LmState>>& candidates);

			template<ArchType arch>
			void addWithoutSearch(size_t morphemeId, Space space, Vector<Candidate<VoidState<arch>>>& candidates);

			template<ArchType arch>
			void addWithoutSearch(U16StringView form, POSTag tag, bool inferRegularity, Space space, Vector<Candidate<VoidState<arch>>>& candidates);
		public:

			~AutoJoiner();
			AutoJoiner(const AutoJoiner&);
			AutoJoiner(AutoJoiner&&);
			AutoJoiner& operator=(const AutoJoiner&);
			AutoJoiner& operator=(AutoJoiner&&);

			void add(size_t morphemeId, Space space = Space::none);
			void add(U16StringView form, POSTag tag, Space space = Space::none);
			void add(const std::u16string& form, POSTag tag, bool inferRegularity = true, Space space = Space::none);
			void add(const char16_t* form, POSTag tag, bool inferRegularity = true, Space space = Space::none);

			std::u16string getU16(std::vector<std::pair<uint32_t, uint32_t>>* rangesOut = nullptr) const;
			std::string getU8(std::vector<std::pair<uint32_t, uint32_t>>* rangesOut = nullptr) const;
		};
	}
}
