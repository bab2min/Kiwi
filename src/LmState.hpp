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

		bool operator==(const VoidState& other) const
		{
			return true;
		}

		float next(const LangModel& lm, size_t next)
		{
			return 0;
		}
	};

	template<ArchType _arch, class VocabTy>
	class KnLMState
	{
		friend struct Hash<KnLMState<_arch, VocabTy>>;
		int32_t node = 0;
	public:
		static constexpr ArchType arch = _arch;

		KnLMState() = default;
		KnLMState(const LangModel& lm) : node{ (int32_t)static_cast<const lm::KnLangModel<arch, VocabTy>&>(*lm.knlm).getBosNodeIdx() } {}

		bool operator==(const KnLMState& other) const
		{
			return node == other.node;
		}

		float next(const LangModel& lm, VocabTy next)
		{
			return static_cast<const lm::KnLangModel<arch, VocabTy>&>(*lm.knlm).progress(node, next);
		}

		void predict(const LangModel& lm, float* out) const
		{
			
		}
	};

	template<size_t windowSize, ArchType _arch, class VocabTy>
	class SbgState : public KnLMState<_arch, VocabTy>
	{
		friend struct Hash<SbgState<windowSize, _arch, VocabTy>>;
		size_t historyPos = 0;
		std::array<VocabTy, windowSize> history = { {0,} };
	public:
		static constexpr ArchType arch = _arch;

		SbgState() = default;
		SbgState(const LangModel& lm) : KnLMState<_arch, VocabTy>{ lm } {}

		bool operator==(const SbgState& other) const
		{
			return KnLMState<_arch, VocabTy>::operator==(other) && historyPos == other.historyPos && history == other.history;
		}

		void getLastHistory(VocabTy* out, size_t n) const
		{
			for (size_t i = 0; i < n; ++i)
			{
				out[i] = history[(historyPos + windowSize + i - n) % windowSize];
			}
		}

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

		void predict(const LangModel& lm, float* out) const
		{

		}
	};

	// hash for LmState
	template<ArchType arch>
	struct Hash<VoidState<arch>>
	{
		size_t operator()(const VoidState<arch>& state) const
		{
			return 0;
		}
	};
	
	template<ArchType arch, class VocabTy>
	struct Hash<KnLMState<arch, VocabTy>>
	{
		size_t operator()(const KnLMState<arch, VocabTy>& state) const
		{
			std::hash<int32_t> hasher;
			return hasher(state.node);
		}
	};

	template<size_t windowSize, ArchType arch, class VocabTy>
	struct Hash<SbgState<windowSize, arch, VocabTy>>
	{
		size_t operator()(const SbgState<windowSize, arch, VocabTy>& state) const
		{
			Hash<KnLMState<arch, VocabTy>> hasher;
			std::hash<VocabTy> vocabHasher;
			size_t ret = hasher(state);
			for (size_t i = 0; i < windowSize; ++i)
			{
				ret = vocabHasher(state.history[i]) ^ ((ret << 3) | (ret >> (sizeof(size_t) * 8 - 3)));
			}
			return ret;
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

	template<class LmStateTy>
	class LmObject : public LmObjectBase
	{
		LangModel mdl;
	public:
		LmObject(const LangModel& _mdl) : mdl(_mdl)
		{
		}

		size_t vocabSize() const override
		{
			return mdl.knlm->getHeader().vocab_size;
		}

		template<class It>
		float evalSequence(It first, It last) const
		{
			float ret = 0;
			LmStateTy state{ mdl };
			for (; first != last; ++first)
			{
				ret += state.next(mdl, *first);
			}
			return ret;
		}

		float evalSequence(const uint32_t* seq, size_t length, size_t stride) const override
		{
			float ret = 0;
			LmStateTy state{ mdl };
			for (size_t i = 0; i < length; ++i)
			{
				ret += state.next(mdl, *seq);
				seq = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(seq) + stride);
			}
			return ret;
		}

		void predictNext(const uint32_t* seq, size_t length, size_t stride, float* outScores) const override
		{
			LmStateTy state{ mdl };
			for (size_t i = 0; i < length; ++i)
			{
				state.next(mdl, *seq);
				seq = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(seq) + stride);
			}
			state.predict(mdl, outScores);
		}

		void evalSequences(
			const uint32_t* prefix, size_t prefixLength, size_t prefixStride,
			const uint32_t* suffix, size_t suffixLength, size_t suffixStride,
			size_t seqSize, const uint32_t** seq, const size_t* seqLength, const size_t* seqStride, float* outScores
		) const override
		{
			float ret = 0;
			LmStateTy state{ mdl };
			for (size_t i = 0; i < prefixLength; ++i)
			{
				ret += state.next(mdl, *prefix);
				prefix = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(prefix) + prefixStride);
			}

			Vector<LmStateTy> states(seqSize, state);
			std::fill(outScores, outScores + seqSize, ret);
			for (size_t s = 0; s < seqSize; ++s)
			{
				auto p = seq[s];
				for (size_t i = 0; i < seqLength[s]; ++i)
				{
					outScores[s] += states[s].next(mdl, *p);
					p = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(p) + seqStride[s]);
				}

				for (size_t i = 0; i < suffixLength; ++i)
				{
					outScores[s] += states[s].next(mdl, *suffix);
					suffix = reinterpret_cast<const uint32_t*>(reinterpret_cast<const uint8_t*>(suffix) + suffixStride);
				}
			}
		}
	};
}
