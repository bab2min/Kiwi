#pragma once

#include <array>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <numeric>

#include "ArchUtils.h"
#include "Mmap.h"

namespace kiwi
{
	namespace pclm
	{
		struct Header
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

		class PCLanguageModelBase
		{
		protected:
			utils::MemoryObject base;

			PCLanguageModelBase(utils::MemoryObject&& mem) : base{ std::move(mem) }
			{
			}
		public:
			virtual ~PCLanguageModelBase() {}
			const Header& getHeader() const { return *reinterpret_cast<const Header*>(base.get()); }

			static utils::MemoryObject build(const std::string& contextDefinition, const std::string& embedding, bool reorderContextIdx = true);
			static std::unique_ptr<PCLanguageModelBase> create(utils::MemoryObject&& mem, ArchType archType = ArchType::none, bool useDistantTokens = false);
		};
	}
}
