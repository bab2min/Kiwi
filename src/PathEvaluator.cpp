#include "PathEvaluator.hpp"

using namespace std;

namespace kiwi
{
	template<template<ArchType> class LmState>
	struct FindBestPathGetter
	{
		template<std::ptrdiff_t i>
		struct Wrapper
		{
			static constexpr FnFindBestPath value = &BestPathFinder::findBestPath<LmState<static_cast<ArchType>(i)>>;
		};
	};


	template<class KeyTy, bool useDistantTokens>
	inline FnFindBestPath getPcLMFindBestPath(ArchType archType, size_t windowSize)
	{
		static tp::Table<FnFindBestPath, AvailableArch> w4{ FindBestPathGetter<WrappedPcLM<4, KeyTy, true, useDistantTokens>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> w7{ FindBestPathGetter<WrappedPcLM<7, KeyTy, true, useDistantTokens>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> w8{ FindBestPathGetter<WrappedPcLM<8, KeyTy, false, useDistantTokens>::type>{} };
		switch (windowSize)
		{
		case 4:
			return w4[static_cast<std::ptrdiff_t>(archType)];
		case 7:
			return w7[static_cast<std::ptrdiff_t>(archType)];
		case 8:
			return w8[static_cast<std::ptrdiff_t>(archType)];
		default:
			throw Exception{ "Unsupported `window_size` : " + to_string(windowSize) };
		}
	}

	FnFindBestPath getFindBestPathFn(ArchType archType, const LangModel& langMdl)
	{
		const auto archIdx = static_cast<std::ptrdiff_t>(archType);
		static tp::Table<FnFindBestPath, AvailableArch> lmKnLM_8{ FindBestPathGetter<WrappedKnLM<uint8_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmKnLM_16{ FindBestPathGetter<WrappedKnLM<uint16_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmKnLM_32{ FindBestPathGetter<WrappedKnLM<uint32_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmKnLMT_8{ FindBestPathGetter<WrappedKnLMTransposed<uint8_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmKnLMT_16{ FindBestPathGetter<WrappedKnLMTransposed<uint16_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmKnLMT_32{ FindBestPathGetter<WrappedKnLMTransposed<uint32_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmSbg_8{ FindBestPathGetter<WrappedSbg<8, uint8_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmSbg_16{ FindBestPathGetter<WrappedSbg<8, uint16_t>::type>{} };
		static tp::Table<FnFindBestPath, AvailableArch> lmSbg_32{ FindBestPathGetter<WrappedSbg<8, uint32_t>::type>{} };

		if (langMdl.type == ModelType::sbg)
		{
			switch (langMdl.sbg->getHeader().keySize)
			{
			case 1:
				return lmSbg_8[archIdx];
			case 2:
				return lmSbg_16[archIdx];
			case 4:
				return lmSbg_32[archIdx];
			default:
				throw Exception{ "Wrong `lmKeySize`" };
			}
		}
		else if (langMdl.type == ModelType::knlm)
		{
			switch (langMdl.knlm->getHeader().key_size)
			{
			case 1:
				return lmKnLM_8[archIdx];
				break;
			case 2:
				return lmKnLM_16[archIdx];
				break;
			case 4:
				return lmKnLM_32[archIdx];
			default:
				throw Exception{ "Wrong `lmKeySize`" };
			}
		}
		else if (langMdl.type == ModelType::knlmTransposed)
		{
			switch (langMdl.knlm->getHeader().key_size)
			{
			case 1:
				return lmKnLMT_8[archIdx];
				break;
			case 2:
				return lmKnLMT_16[archIdx];
				break;
			case 4:
				return lmKnLMT_32[archIdx];
			default:
				throw Exception{ "Wrong `lmKeySize`" };
			}
		}
		else if (langMdl.type == ModelType::pclm)
		{
			switch (langMdl.pclm->getHeader().keySize)
			{
			case 2:
				return getPcLMFindBestPath<uint16_t, true>(archType, langMdl.pclm->getHeader().windowSize);
			case 4:
				return getPcLMFindBestPath<uint32_t, true>(archType, langMdl.pclm->getHeader().windowSize);
			default:
				throw Exception{ "Wrong `lmKeySize`" };
			}
		}
		else if (langMdl.type == ModelType::pclmLocal)
		{
			switch (langMdl.pclm->getHeader().keySize)
			{
			case 2:
				return getPcLMFindBestPath<uint16_t, false>(archType, langMdl.pclm->getHeader().windowSize);
			case 4:
				return getPcLMFindBestPath<uint32_t, false>(archType, langMdl.pclm->getHeader().windowSize);
			default:
				throw Exception{ "Wrong `lmKeySize`" };
			}
		}
		else
		{

		}
		return nullptr;
	}
}
