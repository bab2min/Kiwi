#include "PathEvaluator.hpp"
#include "Joiner.hpp"
#include "Kiwi.hpp"
#include "SkipBigramModel.hpp"

namespace kiwi
{
	template<size_t windowSize, ArchType _arch, class VocabTy>
	struct PathHash<lm::SbgState<windowSize, _arch, VocabTy>>
	{
		using LmState = lm::SbgState<windowSize, _arch, VocabTy>;

		lm::KnLMState<_arch, VocabTy> lmState;
		std::array<VocabTy, 4> lastMorphemes;
		uint8_t rootId, spState;

		PathHash(LmState _lmState = {}, uint8_t _rootId = 0, SpecialState _spState = {})
			: lmState{ _lmState.knlm }, rootId{ _rootId }, spState{ _spState }
		{
			_lmState.getLastHistory(lastMorphemes.data(), lastMorphemes.size());
		}


		PathHash(const WordLL<LmState>& wordLl, const Morpheme* morphBase)
			: PathHash{ wordLl.lmState, wordLl.rootId, wordLl.spState }
		{
		}

		bool operator==(const PathHash& o) const
		{
			return lmState == o.lmState && lastMorphemes == o.lastMorphemes && spState == o.spState;
		}
	};

	template<size_t windowSize, ArchType arch, class VocabTy>
	struct Hash<PathHash<lm::SbgState<windowSize, arch, VocabTy>>>
	{
		size_t operator()(const PathHash<lm::SbgState<windowSize, arch, VocabTy>>& state) const
		{
			size_t ret = 0;
			if (sizeof(state) % sizeof(size_t))
			{
				auto ptr = reinterpret_cast<const uint32_t*>(&state);
				for (size_t i = 0; i < sizeof(state) / sizeof(uint32_t); ++i)
				{
					ret = ptr[i] ^ ((ret << 3) | (ret >> (sizeof(size_t) * 8 - 3)));
				}
			}
			else
			{
				auto ptr = reinterpret_cast<const size_t*>(&state);
				for (size_t i = 0; i < sizeof(state) / sizeof(size_t); ++i)
				{
					ret = ptr[i] ^ ((ret << 3) | (ret >> (sizeof(size_t) * 8 - 3)));
				}
			}
			return ret;
		}
	};

	namespace lm
	{
		template<ArchType arch, class KeyType, size_t windowSize>
		void* SkipBigramModel<arch, KeyType, windowSize>::getFindBestPathFn() const
		{
			return (void*)&BestPathFinder<SkipBigramModel<arch, KeyType, windowSize>>::findBestPath;
		}

		template<ArchType arch, class KeyType, size_t windowSize>
		void* SkipBigramModel<arch, KeyType, windowSize>::getNewJoinerFn() const
		{
			return (void*)&newJoinerWithKiwi<LmStateType>;
		}

		template<ArchType archType>
		std::unique_ptr<SkipBigramModelBase> createOptimizedModel(utils::MemoryObject&& knlmMem, utils::MemoryObject&& sbgMem)
		{
			auto& header = *reinterpret_cast<const SkipBigramModelHeader*>(sbgMem.get());
			switch (header.keySize)
			{
			case 1:
				return make_unique<SkipBigramModel<archType, uint8_t, 8>>(std::move(knlmMem), std::move(sbgMem));
			case 2:
				return make_unique<SkipBigramModel<archType, uint16_t, 8>>(std::move(knlmMem), std::move(sbgMem));
			case 4:
				return make_unique<SkipBigramModel<archType, uint32_t, 8>>(std::move(knlmMem), std::move(sbgMem));
			case 8:
				return make_unique<SkipBigramModel<archType, uint64_t, 8>>(std::move(knlmMem), std::move(sbgMem));
			default:
				throw std::runtime_error{ "Unsupported `key_size` : " + std::to_string((size_t)header.keySize) };
			}
		}

		using FnCreateOptimizedModel = decltype(&createOptimizedModel<ArchType::none>);

		struct CreateOptimizedModelGetter
		{
			template<std::ptrdiff_t i>
			struct Wrapper
			{
				static constexpr FnCreateOptimizedModel value = &createOptimizedModel<static_cast<ArchType>(i)>;
			};
		};

		std::unique_ptr<SkipBigramModelBase> SkipBigramModelBase::create(utils::MemoryObject&& knlmMem, utils::MemoryObject&& sbgMem, ArchType archType)
		{
			static tp::Table<FnCreateOptimizedModel, AvailableArch> table{ CreateOptimizedModelGetter{} };
			auto fn = table[static_cast<std::ptrdiff_t>(archType)];
			if (!fn) throw std::runtime_error{ std::string{"Unsupported architecture : "} + archToStr(archType) };
			return (*fn)(std::move(knlmMem), std::move(sbgMem));
		}
	}
}
