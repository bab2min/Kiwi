#pragma once
#include "Types.h"
#include "ArchUtils.h"
#include "LangModel.h"

namespace kiwi
{
	class Kiwi;
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

			Candidate(const CompiledRule& _cr, const lm::ILangModel* lm)
				: joiner{ _cr }, lmState{ lm }
			{
			}
		};

		template<ArchType arch>
		struct Candidate<lm::VoidState<arch>>
		{
			Joiner joiner;

			Candidate(const CompiledRule& _cr, const lm::ILangModel* lm)
				: joiner{ _cr }
			{
			}
		};

		class ErasedVector
		{
			using FnDestruct = void(*)(ErasedVector*);
			using FnCopyConstruct = void(*)(ErasedVector*, const ErasedVector&);

			template<class T>
			static void destructImpl(ErasedVector* self)
			{
				auto* target = reinterpret_cast<Vector<T>*>(&self->vec);
				std::destroy_at(target);
			}

			template<class T>
			static void copyConstructImpl(ErasedVector* self, const ErasedVector& other)
			{
				auto* target = reinterpret_cast<Vector<T>*>(&self->vec);
				new (target) Vector<T>{ *reinterpret_cast<const Vector<T>*>(&other.vec) };
			}

			union
			{
				Vector<char> vec;
			};
			FnDestruct destruct = nullptr;
			FnCopyConstruct copyConstruct = nullptr;
		public:

			template<class T>
			ErasedVector(Vector<T>&& v)
			{
				auto* target = reinterpret_cast<Vector<T>*>(&vec);
				new (target) Vector<T>{ move(v) };
				destruct = &destructImpl<T>;
				copyConstruct = &copyConstructImpl<T>;
			}

			~ErasedVector()
			{
				if (destruct)
				{
					(*destruct)(this);
					destruct = nullptr;
					copyConstruct = nullptr;
				}
			}

			ErasedVector(const ErasedVector& other)
				: destruct{ other.destruct }, copyConstruct{ other.copyConstruct }
			{
				if (!destruct) return;
				(*copyConstruct)(this, other);
			}

			ErasedVector(ErasedVector&& other)
			{
				std::swap(vec, other.vec);
				std::swap(destruct, other.destruct);
				std::swap(copyConstruct, other.copyConstruct);
			}

			ErasedVector& operator=(const ErasedVector& other)
			{
				this->~ErasedVector();
				new (this) ErasedVector{ other };
				return *this;
			}

			ErasedVector& operator=(ErasedVector&& other)
			{
				std::swap(vec, other.vec);
				std::swap(destruct, other.destruct);
				std::swap(copyConstruct, other.copyConstruct);
				return *this;
			}

			template<class T>
			Vector<T>& get()
			{
				return *reinterpret_cast<Vector<T>*>(&vec);
			}

			template<class T>
			const Vector<T>& get() const
			{
				return *reinterpret_cast<const Vector<T>*>(&vec);
			}
		};

		class AutoJoiner
		{
			friend class kiwi::Kiwi;

			template<class LmState>
			explicit AutoJoiner(const Kiwi& kiwi, Candidate<LmState>&& state);

			template<class Func>
			void foreachMorpheme(const Form* formHead, Func&& func) const;

			template<class LmState>
			void addImpl(size_t morphemeId, Space space, Vector<Candidate<LmState>>& candidates);

			template<class LmState>
			void addImpl2(U16StringView form, POSTag tag, bool inferRegularity, Space space, Vector<Candidate<LmState>>& candidates);

			template<ArchType arch>
			void addWithoutSearchImpl(size_t morphemeId, Space space, Vector<Candidate<lm::VoidState<arch>>>& candidates);

			template<ArchType arch>
			void addWithoutSearchImpl2(U16StringView form, POSTag tag, bool inferRegularity, Space space, Vector<Candidate<lm::VoidState<arch>>>& candidates);

			template<class LmState>
			struct Dispatcher;

			using FnAdd = void(*)(AutoJoiner*, size_t, Space, Vector<Candidate<lm::VoidState<ArchType::none>>>&);
			using FnAdd2 = void(*)(AutoJoiner*, U16StringView, POSTag, bool, Space, Vector<Candidate<lm::VoidState<ArchType::none>>>&);

			const Kiwi* kiwi = nullptr;
			FnAdd dfAdd = nullptr;
			FnAdd2 dfAdd2 = nullptr;
			ErasedVector candBuf;
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
