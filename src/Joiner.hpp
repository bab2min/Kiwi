#pragma once
#include <kiwi/Joiner.h>
#include <kiwi/Kiwi.h>
#include <mapbox/variant.hpp>
#include "Combiner.h"
#include "StrUtils.h"
#include "LmState.hpp"

using namespace std;

namespace kiwi
{
	namespace cmb
	{
		namespace detail
		{
			template<template<ArchType> class Type, class>
			struct VCUnpack2nd;

			template<template<ArchType> class Type, std::ptrdiff_t ...arches>
			struct VCUnpack2nd<Type, tp::seq<arches...>>
			{
				using type = std::tuple<Vector<Candidate<Type<static_cast<ArchType>(arches)>>>...>;
			};

			template<class, template<ArchType> class ... Types>
			struct VCUnpack;

			template<std::ptrdiff_t ...arches, template<ArchType> class ... Types>
			struct VCUnpack<tp::seq<arches...>, Types...>
			{
				using type = TupleCat<typename VCUnpack2nd<Types, tp::seq<arches...>>::type...>;
			};
		}

		using CandTypeTuple = typename detail::VCUnpack<AvailableArch,
			VoidState, 
			WrappedKnLM<uint8_t>::type, WrappedKnLM<uint16_t>::type, WrappedKnLM<uint32_t>::type, WrappedKnLM<uint64_t>::type,
			WrappedSbg<8, uint8_t>::type, WrappedSbg<8, uint16_t>::type, WrappedSbg<8, uint32_t>::type, WrappedSbg<8, uint64_t>::type
		>::type;

		using CandVector = typename detail::VariantFromTuple<CandTypeTuple>::type;

		template<class LmState>
		AutoJoiner::AutoJoiner(const Kiwi& _kiwi, Candidate<LmState>&& state)
			: kiwi{ &_kiwi }
		{
			new (&candBuf) CandVector(Vector<Candidate<LmState>>{ { move(state) } });
		}
	}
}
