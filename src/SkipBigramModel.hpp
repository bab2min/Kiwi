#pragma once

#include <kiwi/Types.h>
#include <kiwi/Utils.h>
#include <kiwi/SkipBigramModel.h>
#include <kiwi/ArchUtils.h>
#include "ArchAvailable.h"
#include "Knlm.hpp"
#include "MathFunc.h"
#include "search.h"

namespace kiwi
{
	namespace lm
	{
		template<size_t windowSize, ArchType _arch, class VocabTy>
		class SbgState;

		template<ArchType arch, class KeyType, size_t windowSize>
		class SkipBigramModel : public SkipBigramModelBase
		{
			friend class SbgState<windowSize, arch, KeyType>;

			KnLangModel<arch, KeyType> knlm;
			std::unique_ptr<size_t[]> ptrs;
			std::unique_ptr<float[]> restoredFloats;
			std::unique_ptr<KeyType[]> keyData;
			std::unique_ptr<uint8_t[]> vocabValidness;
			const float* discnts = nullptr;
			const float* compensations = nullptr;
			float logWindowSize;
		public:
			using VocabType = KeyType;
			using LmStateType = SbgState<windowSize, arch, KeyType>;

			size_t getMemorySize() const override { return base.size() + knlm.getMemorySize(); }
			void* getFindBestPathFn() const override;
			void* getNewJoinerFn() const override;

			SkipBigramModel(utils::MemoryObject&& knlmMem, utils::MemoryObject&& sbgMem) : SkipBigramModelBase{ std::move(sbgMem) }, knlm{ std::move(knlmMem) }
			{
				auto* ptr = reinterpret_cast<const char*>(base.get());
				auto& header = getHeader();

				const KeyType* kSizes = reinterpret_cast<const KeyType*>(ptr += sizeof(SkipBigramModelHeader));
				ptrs = make_unique<size_t[]>(header.vocabSize + 1);
				ptrs[0] = 0;
				for (size_t i = 0; i < header.vocabSize; ++i)
				{
					ptrs[i + 1] = ptrs[i] + kSizes[i];
				}

				size_t totalVocabs = ptrs[header.vocabSize];
				keyData = make_unique<KeyType[]>(totalVocabs);
				restoredFloats = make_unique<float[]>(totalVocabs + (header.quantize ? header.vocabSize : 0));
				vocabValidness = make_unique<uint8_t[]>(header.vocabSize);
				std::fill(vocabValidness.get(), vocabValidness.get() + header.vocabSize, 0);

				auto kdSrc = reinterpret_cast<const KeyType*>(ptr += header.vocabSize * sizeof(KeyType));
				std::copy(kdSrc, kdSrc + totalVocabs, keyData.get());
				
				if (header.quantize)
				{
					auto discntSrc = reinterpret_cast<const uint8_t*>(ptr += totalVocabs * sizeof(KeyType));
					auto cmpSrc = reinterpret_cast<const uint8_t*>(ptr += header.vocabSize * sizeof(uint8_t));
					auto vvSrc = reinterpret_cast<const uint8_t*>(ptr += totalVocabs * sizeof(uint8_t));
					std::copy(vvSrc, vvSrc + header.vocabSize, vocabValidness.get());

					auto discntTable = reinterpret_cast<const float*>(ptr += header.vocabSize * sizeof(uint8_t));
					auto cmpTable = discntTable + 256;

					discnts = restoredFloats.get() + totalVocabs;
					for (size_t i = 0; i < header.vocabSize; ++i)
					{
						restoredFloats[i + totalVocabs] = discntTable[discntSrc[i]];
					}

					compensations = restoredFloats.get();
					for (size_t i = 0; i < totalVocabs; ++i)
					{
						restoredFloats[i] = cmpTable[cmpSrc[i]];
					}
				}
				else
				{
					discnts = reinterpret_cast<const float*>(ptr += totalVocabs * sizeof(KeyType));
					auto cmpSrc = reinterpret_cast<const float*>(ptr += header.vocabSize * sizeof(float));
					std::copy(cmpSrc, cmpSrc + totalVocabs, restoredFloats.get());
					compensations = restoredFloats.get();
					auto vvSrc = reinterpret_cast<const uint8_t*>(ptr += totalVocabs * sizeof(float));
					std::copy(vvSrc, vvSrc + header.vocabSize, vocabValidness.get());
				}
				
				auto mutableCompensations = restoredFloats.get();

				Vector<uint8_t> tempBuf;
				for (size_t i = 0; i < header.vocabSize; ++i)
				{
					size_t size = ptrs[i + 1] - ptrs[i];
					if (!size) continue;
					nst::prepare<arch>(keyData.get() + ptrs[i], mutableCompensations + ptrs[i], size, tempBuf);
				}

				logWindowSize = std::log((float)header.windowSize);
			}

			bool isValidVocab(KeyType k) const
			{
				if (k >= getHeader().vocabSize) return false;
				return !!vocabValidness[k];
			}

			float evaluate(const KeyType* history, size_t cnt, KeyType next, float base) const
			{
				if (!cnt) return base;
				if (!vocabValidness[next]) return base;

#if defined(__GNUC__) && __GNUC__ < 5
				alignas(256) float arr[windowSize * 2];
#else
				alignas(ArchInfo<arch>::alignment) float arr[windowSize * 2];
#endif
				std::fill(arr, arr + windowSize, base);
				std::fill(arr + windowSize, arr + windowSize * 2, -INFINITY);

				size_t b = ptrs[next], e = ptrs[next + 1];
				size_t size = e - b;

				for (size_t i = 0; i < cnt; ++i)
				{
					arr[i] = discnts[history[i]] + base;
					float out;
					if (nst::search<arch>(&keyData[b], &compensations[b], size, history[i], out))
					{
						arr[i + windowSize] = out;
					}
				}
				return logSumExp<arch>(arr, windowSize * 2) - logWindowSize;
			}

		};

		template<size_t windowSize, ArchType _arch, class VocabTy>
		struct SbgState : public LmStateBase<SkipBigramModel<_arch, VocabTy, windowSize>>
		{
			KnLMState<_arch, VocabTy> knlm;
			size_t historyPos = 0;
			std::array<VocabTy, windowSize> history = { {0,} };

			static constexpr ArchType arch = _arch;
			static constexpr bool transposed = false;

			SbgState() = default;
			SbgState(const ILangModel* lm) : knlm{ &static_cast<const SkipBigramModel<_arch, VocabTy, windowSize>*>(lm)->knlm } {}

			bool operator==(const SbgState& other) const
			{
				return knlm == other.knlm && historyPos == other.historyPos && history == other.history;
			}

			void getLastHistory(VocabTy* out, size_t n) const
			{
				for (size_t i = 0; i < n; ++i)
				{
					out[i] = history[(historyPos + windowSize + i - n) % windowSize];
				}
			}

			float nextImpl(const SkipBigramModel<_arch, VocabTy, windowSize>* lm, VocabTy next)
			{
				float ll = lm->knlm.progress(knlm.node, next);
				if (lm->isValidVocab(next))
				{
					if (ll > -13)
					{
						ll = lm->evaluate(history.data(), windowSize, next, ll);
					}
					history[historyPos] = next;
					historyPos = (historyPos + 1) % windowSize;
				}
				return ll;
			}
		};
	}


	template<size_t windowSize, ArchType arch, class VocabTy>
	struct Hash<lm::SbgState<windowSize, arch, VocabTy>>
	{
		size_t operator()(const lm::SbgState<windowSize, arch, VocabTy>& state) const
		{
			Hash<lm::KnLMState<arch, VocabTy>> hasher;
			std::hash<VocabTy> vocabHasher;
			size_t ret = hasher(state.knlm);
			for (size_t i = 0; i < windowSize; ++i)
			{
				ret = vocabHasher(state.history[i]) ^ ((ret << 3) | (ret >> (sizeof(size_t) * 8 - 3)));
			}
			return ret;
		}
	};

}
