#pragma once

#include <array>
#include <vector>
#include <deque>
#include <memory>
#include <algorithm>
#include <numeric>

#include "Utils.h"
#include "Mmap.h"
#include "ArchUtils.h"
#include "Types.h"

namespace kiwi
{
	namespace lm
	{
		class ILangModel
		{
		public:
			virtual ~ILangModel() = default;
			virtual ModelType getType() const = 0;
			virtual size_t vocabSize() const = 0;
			virtual size_t getMemorySize() const = 0;

			virtual void* getFindBestPathFn() const = 0;
			virtual void* getNewJoinerFn() const = 0;
		};

		template<class DerivedLM>
		struct LmStateBase
		{
			float next(const ILangModel* langMdl, typename DerivedLM::VocabType nextToken)
			{
				using LmStateType = typename DerivedLM::LmStateType;
				return static_cast<LmStateType*>(this)->nextImpl(static_cast<const DerivedLM*>(langMdl), nextToken);
			}
		};

		template<ArchType arch>
		class VoidLangModel;

		template<ArchType arch>
		struct VoidState : public LmStateBase<VoidLangModel<arch>>
		{
			bool operator==(const VoidState& other) const
			{
				return true;
			}

			float nextImpl(const VoidLangModel<arch>* langMdl, uint32_t nextToken)
			{
				return 0;
			}
		};

		template<ArchType arch>
		class VoidLangModel : public ILangModel
		{
		public:
			using VocabType = uint32_t;
			using LmStateType = VoidState<arch>;

			ModelType getType() const override { return ModelType::none; }
			size_t vocabSize() const override { return 0; }
			void* getFindBestPathFn() const override { return nullptr; }
			void* getNewJoinerFn() const override { return nullptr; }
		};
	}

	template<ArchType arch>
	struct Hash<lm::VoidState<arch>>
	{
		size_t operator()(const lm::VoidState<arch>& state) const
		{
			return 0;
		}
	};
}
