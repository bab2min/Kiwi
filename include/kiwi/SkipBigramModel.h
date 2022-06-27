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
	namespace sb
	{
		struct Header
		{
			uint64_t vocabSize;
			uint8_t keySize, windowSize, compressed, quantize, _rsv[4];
		};

		class SkipBigramModelBase
		{
		protected:
			utils::MemoryObject base;

			SkipBigramModelBase(utils::MemoryObject&& mem) : base{ std::move(mem) }
			{
			}
		public:
			virtual ~SkipBigramModelBase() {}
			const Header& getHeader() const { return *reinterpret_cast<const Header*>(base.get()); }

			static std::unique_ptr<SkipBigramModelBase> create(utils::MemoryObject&& mem, ArchType archType = ArchType::none);
		};
	}
}
