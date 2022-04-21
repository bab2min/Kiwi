#pragma once

#include <Eigen/Dense>

#include <kiwi/Types.h>
#include <kiwi/Utils.h>
#include <kiwi/SkipBigramModel.h>
#include <kiwi/ArchUtils.h>
#include "ArchAvailable.h"
#include "search.h"

namespace kiwi
{
	namespace sb
	{
		template<ArchType arch, class KeyType>
		class SkipBigramModel : public SkipBigramModelBase
		{
			std::unique_ptr<size_t[]> ptrs;
			std::unique_ptr<float[]> restoredFloats;
			std::unique_ptr<KeyType[]> keyData;
			const float* discnts = nullptr;
			const float* compensations = nullptr;
			float logWindowSize;
		public:
			SkipBigramModel(utils::MemoryObject&& mem) : SkipBigramModelBase{ std::move(mem) }
			{
				auto* ptr = reinterpret_cast<const char*>(base.get());
				auto& header = getHeader();

				const KeyType* kSizes = reinterpret_cast<const KeyType*>(ptr += sizeof(Header));
				ptrs = make_unique<size_t[]>(header.vocabSize + 1);
				ptrs[0] = 0;
				for (size_t i = 0; i < header.vocabSize; ++i)
				{
					ptrs[i + 1] = ptrs[i] + kSizes[i];
				}

				size_t totalVocabs = ptrs[header.vocabSize];
				keyData = make_unique<KeyType[]>(totalVocabs);
				restoredFloats = make_unique<float[]>(totalVocabs);

				auto kdSrc = reinterpret_cast<const KeyType*>(ptr += header.vocabSize * sizeof(KeyType));
				std::copy(kdSrc, kdSrc + totalVocabs, keyData.get());
				discnts = reinterpret_cast<const float*>(ptr += totalVocabs * sizeof(KeyType));

				auto cmpSrc = reinterpret_cast<const float*>(ptr += header.vocabSize * sizeof(float));
				std::copy(cmpSrc, cmpSrc + totalVocabs, restoredFloats.get());
				compensations = restoredFloats.get();
				auto mutableCompensations = restoredFloats.get();

				Vector<uint8_t> tempBuf;
				for (size_t i = 0; i < header.vocabSize; ++i)
				{
					size_t size = ptrs[i + 1] - ptrs[i];
					if (!size) continue;
					nst::prepare<arch>(keyData.get() + ptrs[i], mutableCompensations + ptrs[i], size, tempBuf);
				}

				logWindowSize = std::log(header.windowSize);
			}

			float evaluate(const KeyType* history, size_t cnt, KeyType next, float base) const
			{
				if (!cnt) return base;

				Eigen::Array<float, 16, 1> arr;
				arr.template head<8>().fill(base);
				arr.template tail<8>().fill(-INFINITY);

				size_t b = ptrs[next], e = ptrs[next + 1];
				size_t size = e - b;

				for (size_t i = 0; i < cnt; ++i)
				{
					arr[i] = discnts[history[i]] + base;
					float out;
					if (nst::search<arch>(&keyData[b], &compensations[b], size, history[i], out))
					{
						arr[i + 8] = out;
					}
				}
				return std::log((arr - arr.maxCoeff()).exp().sum()) + arr.maxCoeff() - logWindowSize;
			}
		};

		inline std::unique_ptr<SkipBigramModelBase> SkipBigramModelBase::create(utils::MemoryObject&& mem, ArchType archType)
		{
			auto& header = *reinterpret_cast<const Header*>(mem.get());
			switch (header.keySize)
			{
			case 1:
				return make_unique<SkipBigramModel<ArchType::avx2, uint8_t>>(std::move(mem));
			case 2:
				return make_unique<SkipBigramModel<ArchType::avx2, uint16_t>>(std::move(mem));
			case 4:
				return make_unique<SkipBigramModel<ArchType::avx2, uint32_t>>(std::move(mem));
			case 8:
				return make_unique<SkipBigramModel<ArchType::avx2, uint64_t>>(std::move(mem));
			default:
				throw std::runtime_error{ "Unsupported `key_size` : " + std::to_string((size_t)header.keySize) };
			}
		}
	}
}
