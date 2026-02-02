#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <bitset>
#include <vector>

namespace kiwi
{
	class SubstringCounter
	{
		struct Entry
		{
			const char16_t* ptr = nullptr;
			uint32_t hash = 0;
			uint16_t length = 0;
			uint16_t count = 0;
		};

		std::vector<Entry> table;
		uint32_t mask = 0;
		size_t entryCount = 0;
		std::vector<char16_t> chars;

		static constexpr uint32_t kPrime = 0x01000193;
		static constexpr uint32_t kOffset = 0x811c9dc5;

		void grow()
		{
			size_t newSize = table.size() * 2;
			uint32_t newMask = (uint32_t)(newSize - 1);
			std::vector<Entry> newTable(newSize);
			for (auto& e : table)
			{
				if (!e.ptr) continue;
				size_t slot = e.hash & newMask;
				while (newTable[slot].ptr)
				{
					slot = (slot + 1) & newMask;
				}
				newTable[slot] = e;
			}
			table = std::move(newTable);
			mask = newMask;
		}

		void insertOrIncrement(uint32_t hash, const char16_t* ptr, size_t length)
		{
			if (entryCount * 5 > table.size() * 3) // load factor > 0.6
			{
				grow();
			}

			size_t slot = hash & mask;
			while (true)
			{
				auto& e = table[slot];
				if (!e.ptr)
				{
					e.hash = hash;
					e.ptr = ptr;
					e.length = (uint16_t)length;
					e.count = 1;
					++entryCount;
					return;
				}
				if (e.hash == hash && e.length == length &&
					std::memcmp(e.ptr, ptr, length * sizeof(char16_t)) == 0)
				{
					++e.count;
					return;
				}
				slot = (slot + 1) & mask;
			}
		}

	public:
		SubstringCounter() = default;

		SubstringCounter(const char16_t* data, size_t size, size_t maxLen = 32)
		{
			// estimate initial table size
			size_t estimatedEntries = size * 8;
			size_t tableSize = 16;
			while (tableSize < estimatedEntries * 2) tableSize *= 2;
			table.resize(tableSize);
			mask = (uint32_t)(tableSize - 1);
			entryCount = 0;

			// collect unique chars
			std::bitset<0x10000> seen;

			size_t segStart = 0;
			for (size_t s = 0; s <= size; ++s)
			{
				if (s == size || data[s] == u' ')
				{
					for (size_t i = segStart; i < s; ++i)
					{
						uint32_t rollingHash = 0;
						const size_t jEnd = std::min(i + maxLen, s);
						for (size_t j = i; j < jEnd; ++j)
						{
							const auto c = data[j];
							if (j == i)
								rollingHash = initHash(c);
							else
								rollingHash = extendHash(rollingHash, c);

							insertOrIncrement(rollingHash, &data[i], j - i + 1);

							if (!seen[c])
							{
								seen[c] = true;
							}
						}
					}
					segStart = s + 1;
				}
			}

			// build sorted unique chars vector
			for (size_t i = 0; i < 0x10000; ++i)
			{
				if (seen[i])
				{
					chars.push_back((char16_t)i);
				}
			}
		}

		size_t count(uint32_t hash, const char16_t* data, size_t len) const
		{
			if (table.empty()) return 0;
			size_t slot = hash & mask;
			while (true)
			{
				auto& e = table[slot];
				if (!e.ptr) return 0;
				if (e.hash == hash && e.length == len &&
					std::memcmp(e.ptr, data, len * sizeof(char16_t)) == 0)
				{
					return e.count;
				}
				slot = (slot + 1) & mask;
			}
		}

		const std::vector<char16_t>& getUniqueChars() const
		{
			return chars;
		}

		static uint32_t initHash(char16_t c)
		{
			return (uint32_t)c * kPrime + kOffset;
		}

		static uint32_t extendHash(uint32_t prev, char16_t c)
		{
			return prev * kPrime + (uint32_t)c;
		}
	};
}

