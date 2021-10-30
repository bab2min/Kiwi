#pragma once
#include <kiwi/ArchUtils.h>

namespace kiwi
{
	namespace utils
	{
		namespace detail
		{
			template<ArchType arch, class IntTy>
			bool bsearchImpl(const IntTy* keys, size_t size, IntTy target, size_t& ret);
		}

		template<ArchType arch, class IntTy, class ValueIt, class Out>
		bool bsearch(const IntTy* keys, ValueIt values, size_t size, IntTy target, Out& ret)
		{
			size_t idx;
			if (detail::bsearchImpl<arch, IntTy>(keys, size, target, idx))
			{
				ret = values[idx];
				return true;
			}
			else return false;
		}
	}
}
