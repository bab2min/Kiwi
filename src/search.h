#pragma once
#include <kiwi/ArchUtils.h>
#include <kiwi/Types.h>

namespace kiwi
{
	namespace nst
	{
		namespace detail
		{
			template<ArchType arch, class IntTy>
			bool searchImpl(const IntTy* keys, size_t size, IntTy target, size_t& ret);

			template<ArchType arch, class IntTy>
			Vector<size_t> reorderImpl(const IntTy* keys, size_t size);
		}

		template<ArchType arch, class IntTy, class Value>
		void prepare(IntTy* keys, Value* values, size_t size)
		{
			auto order = detail::reorderImpl<arch>(keys, size);
			if (order.empty()) return;

			Vector<IntTy> tempKeys{ std::make_move_iterator(keys), std::make_move_iterator(keys + size) };
			Vector<Value> tempValues{ std::make_move_iterator(values), std::make_move_iterator(values + size) };

			for (size_t i = 0; i < size; ++i)
			{
				keys[i] = std::move(tempKeys[order[i]]);
				values[i] = std::move(tempValues[order[i]]);
			}
		}

		template<ArchType arch, class IntTy, class Value, class Out>
		bool search(const IntTy* keys, const Value* values, size_t size, IntTy target, Out& ret)
		{
			size_t idx;
			if (detail::searchImpl<arch>(keys, size, target, idx))
			{
				ret = values[idx];
				return true;
			}
			else return false;
		}
	}
}
