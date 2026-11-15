#pragma once

#include <vector>
#include <algorithm>
#include <type_traits>
#include <tuple>
#include <functional>

namespace kiwi
{
	namespace utils
	{
		class ContainerSearcher
		{
			const size_t* idcs;
			const size_t size;
		public:
			template<class Alloc>
			ContainerSearcher(const std::vector<size_t, Alloc>& _idcs)
				: idcs(_idcs.data()), size(_idcs.size())
			{
			}

			size_t operator()(size_t v) const
			{
				return std::upper_bound(idcs, idcs + size, v) - idcs - 1;
			}
		};
	}
}
