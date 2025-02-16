#pragma once

#include <array>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <numeric>

#include "ArchUtils.h"
#include "Mmap.h"
#include "LangModel.h"

namespace kiwi
{
	namespace lm
	{
		struct PcLangModelHeader
		{
			uint64_t vocabSize, contextSize;
			uint16_t dim;
			uint8_t contextType, outputType;
			uint8_t keySize, windowSize, quantize, _reserved;
			uint64_t numNodes;
			uint64_t nodeOffset, keyOffset, valueOffset, embOffset;
		};

		template<class KeyType, class ValueType, class DiffType = int32_t>
		struct Node
		{
			KeyType numNexts = 0;
			ValueType value = 0;
			DiffType lower = 0;
			uint32_t nextOffset = 0;
		};

		class PcLangModelBase : public ILangModel
		{
		protected:
			const size_t memorySize = 0;
			PcLangModelHeader header;

			PcLangModelBase(const utils::MemoryObject& mem) : memorySize{ mem.size() }, header{ *reinterpret_cast<const PcLangModelHeader*>(mem.get()) }
			{
			}
		public:
			virtual ~PcLangModelBase() {}
			size_t vocabSize() const override { return header.vocabSize; }
			size_t getMemorySize() const override { return memorySize; }

			const PcLangModelHeader& getHeader() const { return header; }

			static utils::MemoryObject build(const std::string& contextDefinition, const std::string& embedding, size_t maxContextLength = -1, bool useVLE = true, bool reorderContextIdx = true);
			static std::unique_ptr<PcLangModelBase> create(utils::MemoryObject&& mem, ArchType archType = ArchType::none, bool useDistantTokens = false, bool quantized = true);
		};
	}
}
