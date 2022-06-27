#pragma once

#include <kiwi/Kiwi.h>
#include <kiwi/ArchUtils.h>
#include "Knlm.hpp"
#include "SkipBigramModel.hpp"

namespace kiwi
{
	template<ArchType _arch>
	class VoidState
	{
	public:
		static constexpr ArchType arch = _arch;

		VoidState() = default;
		VoidState(const LangModel& lm) {}
	};

	template<ArchType _arch, class VocabTy>
	class KnLMState
	{
		ptrdiff_t node = 0;
	public:
		static constexpr ArchType arch = _arch;

		KnLMState() = default;
		KnLMState(const LangModel& lm) : node{ static_cast<const lm::KnLangModel<arch, VocabTy>&>(*lm.knlm).getBosNodeIdx() } {}

		float next(const LangModel& lm, VocabTy next)
		{
			return static_cast<const lm::KnLangModel<arch, VocabTy>&>(*lm.knlm).progress(node, next);
		}
	};

	template<size_t windowSize, ArchType _arch, class VocabTy>
	class SbgState : KnLMState<_arch, VocabTy>
	{
		size_t historyPos = 0;
		std::array<VocabTy, windowSize> history = { {0,} };
	public:
		static constexpr ArchType arch = _arch;

		SbgState() = default;
		SbgState(const LangModel& lm) : KnLMState<_arch, VocabTy>{ lm } {}

		float next(const LangModel& lm, VocabTy next)
		{
			auto& sbg = static_cast<const sb::SkipBigramModel<arch, VocabTy, 8>&>(*lm.sbg);
			float ll = KnLMState<arch, VocabTy>::next(lm, next);
			if (sbg.isValidVocab(next))
			{
				if (ll > -13)
				{
					ll = sbg.evaluate(history.data(), windowSize, next, ll);
				}
				history[historyPos] = next;
				historyPos = (historyPos + 1) % windowSize;
			}
			return ll;
		}
	};


	template<class VocabTy>
	struct WrappedKnLM
	{
		template<ArchType arch> using type = KnLMState<arch, VocabTy>;
	};

	template<size_t windowSize, class VocabTy>
	struct WrappedSbg
	{
		template<ArchType arch> using type = SbgState<windowSize, arch, VocabTy>;
	};

}
