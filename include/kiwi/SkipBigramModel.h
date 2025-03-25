#pragma once

#include "Knlm.h"

namespace kiwi
{
	namespace lm
	{
		struct SkipBigramModelHeader
		{
			uint64_t vocabSize;
			uint8_t keySize, windowSize, compressed, quantize, _rsv[4];
		};

		class SkipBigramModelBase : public ILangModel
		{
		protected:
			utils::MemoryObject base;

			SkipBigramModelBase(utils::MemoryObject&& mem) : base{ std::move(mem) }
			{
			}
		public:
			virtual ~SkipBigramModelBase() {}
			size_t vocabSize() const override { return getHeader().vocabSize; }
			ModelType getType() const override { return ModelType::sbg; }

			const SkipBigramModelHeader& getHeader() const { return *reinterpret_cast<const SkipBigramModelHeader*>(base.get()); }

			static std::unique_ptr<SkipBigramModelBase> create(utils::MemoryObject&& knlmMem, utils::MemoryObject&& sbgMem, ArchType archType = ArchType::none);
		};
	}
}
