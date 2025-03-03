#pragma once
#include <cstring>
#include <kiwi/ArchUtils.h>
#include <kiwi/Types.h>

#if (defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_AMD64))) || defined(__INTEL_COMPILER) || MM_PREFETCH 
#include <xmmintrin.h> 
#define PREFETCH_T0(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0) 
#elif defined(__GNUC__) 
#define PREFETCH_T0(addr) __builtin_prefetch(reinterpret_cast<const char*>(addr), 0, 3) 
#else 
#define PREFETCH_T0(addr) do {} while (0) 
#endif 

namespace kiwi
{
	namespace nst
	{
		namespace detail
		{
			template<ArchType arch, class IntTy>
			bool searchImpl(const IntTy* keys, size_t size, IntTy target, size_t& ret);

			template<ArchType arch, class IntTy, class ValueTy>
			ValueTy searchKVImpl(const void* keys, size_t size, IntTy target);

			template<ArchType arch, class IntTy>
			Vector<size_t> reorderImpl(const IntTy* keys, size_t size);

			template<ArchType arch>
			size_t getPacketSizeImpl();

			template<ArchType arch>
			size_t findAllImpl(const uint8_t* arr, size_t size, uint8_t key);
		}

		template<ArchType arch, class IntTy, class Value>
		void prepare(IntTy* keys, Value* values, size_t size, Vector<uint8_t>& tempBuf)
		{
			if (size <= 1) return;
			auto order = detail::reorderImpl<arch>(keys, size);
			if (order.empty()) return;

			if (tempBuf.size() < std::max(sizeof(IntTy), sizeof(Value)) * size)
			{
				tempBuf.resize(std::max(sizeof(IntTy), sizeof(Value)) * size);
			}
			auto tempKeys = (IntTy*)tempBuf.data();
			auto tempValues = (Value*)tempBuf.data();
			std::copy(keys, keys + size, tempKeys);
			for (size_t i = 0; i < size; ++i)
			{
				keys[i] = tempKeys[order[i]];
			}

			std::copy(values, values + size, tempValues);
			for (size_t i = 0; i < size; ++i)
			{
				values[i] = tempValues[order[i]];
			}
		}

		template<ArchType arch, class IntTy, class Value>
		void prepareKV(void* dest, size_t size, Vector<uint8_t>& tempBuf)
		{
			const size_t packetSize = detail::getPacketSizeImpl<arch>() / sizeof(IntTy);
			if (size <= 1 || packetSize <= 1) return;
			auto order = detail::reorderImpl<arch>(reinterpret_cast<IntTy*>(dest), size);
			if (order.empty()) return;

			if (tempBuf.size() < (sizeof(IntTy) + sizeof(Value)) * size)
			{
				tempBuf.resize((sizeof(IntTy) + sizeof(Value)) * size);
			}
			std::memcpy(tempBuf.data(), dest, (sizeof(IntTy) + sizeof(Value)) * size);
			auto tempKeys = (IntTy*)tempBuf.data();
			auto tempValues = (Value*)(tempKeys + size);
			for (size_t i = 0; i < size; i += packetSize)
			{
				const size_t groupSize = std::min(packetSize, size - i);
				for (size_t j = 0; j < groupSize; ++j)
				{
					*reinterpret_cast<IntTy*>(dest) = tempKeys[order[i + j]];
					dest = reinterpret_cast<uint8_t*>(dest) + sizeof(IntTy);
				}
				for (size_t j = 0; j < groupSize; ++j)
				{
					*reinterpret_cast<Value*>(dest) = tempValues[order[i + j]];
					dest = reinterpret_cast<uint8_t*>(dest) + sizeof(Value);
				}
			}
		}

		template<ArchType arch, class IntTy, class Value>
		std::pair<IntTy, Value> extractKV(const void* kv, size_t totSize, size_t idx)
		{
			const size_t packetSize = detail::getPacketSizeImpl<arch>() / sizeof(IntTy);
			if (packetSize <= 1)
			{
				const auto* key = reinterpret_cast<const IntTy*>(kv);
				const auto* value = reinterpret_cast<const Value*>(key + totSize);
				return std::make_pair(key[idx], value[idx]);
			}

			const size_t groupIdx = idx / packetSize;
			const size_t groupOffset = idx % packetSize;
			const auto* group = reinterpret_cast<const uint8_t*>(kv) + groupIdx * packetSize * (sizeof(IntTy) + sizeof(Value));
			const auto* key = reinterpret_cast<const IntTy*>(group);
			const auto* value = reinterpret_cast<const Value*>(key + std::min(packetSize, totSize - groupIdx * packetSize));
			return std::make_pair(key[groupOffset], value[groupOffset]);
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

		template<ArchType arch, class IntTy, class Out>
		bool search(const IntTy* keys, size_t size, IntTy target, Out& ret)
		{
			size_t idx;
			if (detail::searchImpl<arch>(keys, size, target, idx))
			{
				ret = idx;
				return true;
			}
			else return false;
		}

		template<ArchType arch, class IntTy, class Value, class Out>
		Out searchKV(const void* kv, size_t size, IntTy target)
		{
			return detail::searchKVImpl<arch, IntTy, typename UnsignedType<Out>::type>(kv, size, target);
		}

		template<ArchType arch>
		size_t findAll(const uint8_t* arr, size_t size, uint8_t key)
		{
			if (size == 0) return 0;
			return detail::findAllImpl<arch>(arr, size, key);
		}
	}
}
