#pragma once

#include <vector>
#include <memory>

#include <kiwi/BitUtils.h>

namespace sais
{
	inline size_t popcntBits(const uint8_t* bits, size_t bitSize)
	{
		using namespace kiwi::utils;

		static constexpr size_t rSize = sizeof(size_t) * 8;
		if (bitSize == 0) return 0;
		const size_t blocks = bitSize / rSize;
		const size_t tailSize = bitSize & (rSize - 1);
		size_t ret = 0;
		for (size_t i = 0; i < blocks; ++i)
		{
			ret += popcount(((const size_t*)bits)[i]);
		}
		if (tailSize) ret += popcount(((const size_t*)bits)[blocks] << (rSize - tailSize));
		return ret;
	}

	template<class ChrTy>
	inline size_t writeLSBs(uint8_t* out, size_t bitOffset, const ChrTy* data, size_t size)
	{
		using namespace kiwi::utils;

		static constexpr size_t rSize = sizeof(size_t) * 8;
		size_t headSize = std::min(bitOffset ? (rSize - bitOffset) : 0, size);
		size_t bodySize = (std::max(size, headSize) - headSize) & ~(rSize - 1);
		size_t tailSize = size - headSize - bodySize;
		size_t oneCnt = 0;

		for (size_t i = 0; i < headSize; ++i)
		{
			auto v = (size_t)(data[i] & 1);
			((size_t*)out)[0] |= v << (i + bitOffset);
			oneCnt += v ? 1 : 0;
		}

		for (size_t i = 0; i < bodySize / rSize; ++i)
		{
			auto curData = data + headSize + i * rSize;
			size_t g = 0;
			for (size_t j = 0; j < rSize; ++j)
			{
				auto v = (size_t)(curData[j] & 1);
				g |= v << j;
			}
			((size_t*)out)[i + (headSize ? 1 : 0)] = g;
			oneCnt += popcount(g);
		}

		if (tailSize)
		{
			auto& tail = ((size_t*)out)[bodySize / rSize + (headSize ? 1 : 0)];
			tail = 0;
			for (size_t i = 0; i < tailSize; ++i)
			{
				auto v = (size_t)(data[headSize + bodySize + i] & 1);
				tail |= v << (i & (rSize - 1));
				oneCnt += v ? 1 : 0;
			}
		}
		return oneCnt;
	}

	template<class ChrTy>
	inline void splitByLSB(ChrTy* ones, ChrTy* zeros, const ChrTy* data, size_t size)
	{
		for (size_t i = 0; i < size; ++i)
		{
			if ((*data & 1))
			{
				*ones++ = *data >> 1;
			}
			else
			{
				*zeros++ = *data >> 1;
			}
			++data;
		}
	}

	template<class ChrTy>
	inline size_t writeMSBs(uint8_t* out, size_t bitOffset, const ChrTy* data, size_t size)
	{
		using namespace kiwi::utils;
		static constexpr size_t bitSize = sizeof(ChrTy) * 8;
		static constexpr size_t rSize = sizeof(size_t) * 8;
		size_t headSize = std::min(bitOffset ? (rSize - bitOffset) : 0, size);
		size_t bodySize = (std::max(size, headSize) - headSize) & ~(rSize - 1);
		size_t tailSize = size - headSize - bodySize;
		size_t oneCnt = 0;

		for (size_t i = 0; i < headSize; ++i)
		{
			auto v = (size_t)((data[i] & (1 << (bitSize - 1))) ? 1 : 0);
			((size_t*)out)[0] |= v << (i + bitOffset);
			oneCnt += v ? 1 : 0;
		}

		for (size_t i = 0; i < bodySize / rSize; ++i)
		{
			auto curData = data + headSize + i * rSize;
			size_t g = 0;
			for (size_t j = 0; j < rSize; ++j)
			{
				auto v = (size_t)((curData[j] & (1 << (bitSize - 1))) ? 1 : 0);
				g |= v << j;
			}
			((size_t*)out)[i + (headSize ? 1 : 0)] = g;
			oneCnt += popcount(g);
		}

		if (tailSize)
		{
			auto& tail = ((size_t*)out)[bodySize / rSize + (headSize ? 1 : 0)];
			tail = 0;
			for (size_t i = 0; i < tailSize; ++i)
			{
				auto v = (size_t)((data[headSize + bodySize + i] & (1 << (bitSize - 1))) ? 1 : 0);
				tail |= v << (i & (rSize - 1));
				oneCnt += v ? 1 : 0;
			}
		}
		return oneCnt;
	}

	template<class ChrTy>
	inline void splitByMSB(ChrTy* ones, ChrTy* zeros, const ChrTy* data, size_t size)
	{
		static constexpr size_t bitSize = sizeof(ChrTy) * 8;
		for (size_t i = 0; i < size; ++i)
		{
			if ((*data & (1 << (bitSize - 1))))
			{
				*ones++ = *data << 1;
			}
			else
			{
				*zeros++ = *data << 1;
			}
			++data;
		}
	}

	template<size_t superBlockSize>
	inline void fillSuperBlocks(size_t* superBlocks, const uint8_t* bits, size_t size)
	{
		using namespace kiwi::utils;

		size_t acc = 0;
		size &= ~(superBlockSize - 1);
		for (size_t i = 0; i < size; i += superBlockSize)
		{
			auto curBits = (const size_t*)(bits + i);
			for (size_t j = 0; j < superBlockSize / sizeof(size_t); ++j)
			{
				acc += popcount(curBits[j]);
			}
			*superBlocks++ = acc;
		}
	}

	inline size_t toBinaryTreeOrder(size_t n, size_t k)
	{
		if (n == 0) return 0;
		size_t h = 0;
		for (; (n & 1) == 0; h++, n >>= 1);
		return n / 2 + ((size_t)1 << (k - h - 1));
	}

	template<class ChrTy>
	class WaveletTree
	{
		static constexpr size_t bitAlignmentSize = sizeof(size_t) * 8;
		static constexpr size_t superBlockSize = 64;
		static constexpr size_t superBlockBitSize = superBlockSize * 8;
		static constexpr size_t rSize = sizeof(size_t) * 8;
		static constexpr size_t depth = sizeof(ChrTy) * 8;

		size_t length = 0;
		std::unique_ptr<uint8_t[]> bits;
		std::unique_ptr<size_t[]> offsets;
		std::unique_ptr<size_t[]> superBlocks;

		size_t countOne(const uint8_t* curBits, const size_t* curSuperBlocks, size_t p) const
		{
			const size_t superBlock = p / superBlockBitSize;
			const size_t tailSize = p & (superBlockBitSize - 1);
			return (superBlock ? curSuperBlocks[superBlock - 1] : 0) + popcntBits(&curBits[superBlock * superBlockSize], tailSize);
		}

		template<class Fn>
		size_t enumerate(size_t i, ChrTy c, size_t l, size_t r, size_t offsetIdx, Fn&& fn) const
		{
			size_t ret = 0;

			const size_t alignedSize = (length + bitAlignmentSize - 1) & ~(bitAlignmentSize - 1);

			auto curBits = &bits[i * alignedSize / 8];
			auto curSuperBlocks = &superBlocks[i * (alignedSize / superBlockBitSize)];
			const size_t offset = offsets[offsetIdx];
			const size_t lOneCnt = countOne(curBits, curSuperBlocks, l + offset) - countOne(curBits, curSuperBlocks, offset);
			const size_t rOneCnt = countOne(curBits, curSuperBlocks, r + offset) - countOne(curBits, curSuperBlocks, offset);
			const size_t oneCnt = rOneCnt - lOneCnt;
			const size_t zeroCnt = r - l - oneCnt;
			if (i + 1 < depth)
			{
				if (zeroCnt)
				{
					ret += enumerate(i + 1, c << 1, l - lOneCnt, r - rOneCnt, offsetIdx + ((size_t)1 << (depth - i - 1)), fn);
				}

				if (oneCnt)
				{
					ret += enumerate(i + 1, (c << 1) | 1, lOneCnt, rOneCnt, offsetIdx, fn);
				}
			}
			else
			{
				if (zeroCnt)
				{
					fn(c << 1, l - lOneCnt, r - rOneCnt);
					ret++;
				}

				if (oneCnt)
				{
					fn((c << 1) | 1, lOneCnt, rOneCnt);
					ret++;
				}
			}
			return ret;
		}

	public:
		WaveletTree() = default;

		WaveletTree(const ChrTy* data, size_t size)
		{
			const size_t alignedSize = (size + bitAlignmentSize - 1) & ~(bitAlignmentSize - 1);
			length = size;
			bits = std::unique_ptr<uint8_t[]>(new uint8_t[alignedSize * sizeof(ChrTy)]);
			offsets = std::unique_ptr<size_t[]>(new size_t[(size_t)1 << depth]);
			if (alignedSize / superBlockBitSize > 0) superBlocks = std::unique_ptr<size_t[]>(new size_t[alignedSize / superBlockBitSize * depth]);

			std::vector<ChrTy> buf(size * 2);
			size_t oneCnt = writeMSBs(&bits[0], 0, data, size);
			fillSuperBlocks<superBlockSize>(&superBlocks[0], &bits[0], alignedSize / 8);
			splitByMSB(&buf[0], &buf[oneCnt], data, size);
			offsets[0] = 0;
			offsets[(size_t)1 << (depth - 1)] = oneCnt;
			for (size_t i = 1; i < depth; ++i)
			{
				const size_t gSize = (size_t)1 << i;
				auto curBuf = &buf[(i & 1) ? 0 : size];
				auto anotherBuf = &buf[(i & 1) ? size : 0];
				auto curBits = &bits[i * alignedSize / 8];
				auto curSuperBlocks = &superBlocks[i * (alignedSize / superBlockBitSize)];
				for (size_t j = 0; j < gSize; ++j)
				{
					size_t start = offsets[j << (depth - i)];
					size_t end = (j == gSize - 1) ? size : offsets[(j + 1) << (depth - i)];

					oneCnt = writeMSBs(&curBits[start / rSize * sizeof(size_t)], start & (rSize - 1), &curBuf[start], end - start);
					offsets[(j << (depth - i)) + ((size_t)1 << (depth - i - 1))] = start + oneCnt;
					if (i < depth - 1)
					{
						splitByMSB(&anotherBuf[start], &anotherBuf[start + oneCnt], &curBuf[start], end - start);
					}
				}
				fillSuperBlocks<superBlockSize>(curSuperBlocks, curBits, alignedSize / 8);
			}
		}

		size_t rank(ChrTy c, size_t l) const
		{
			const size_t alignedSize = (length + bitAlignmentSize - 1) & ~(bitAlignmentSize - 1);
			size_t offsetIdx = 0;
			for (size_t i = 0; i < depth; ++i)
			{
				if (l == 0) break;
				auto curBits = &bits[i * alignedSize / 8];
				auto curSuperBlocks = &superBlocks[i * (alignedSize / superBlockBitSize)];
				const size_t offset = offsets[offsetIdx];

				const size_t oneCnt = countOne(curBits, curSuperBlocks, l + offset) - countOne(curBits, curSuperBlocks, offset);
				const size_t msb = (c & (1 << (depth - 1)));
				l = msb ? oneCnt : (l - oneCnt);
				offsetIdx += msb ? 0 : ((size_t)1 << (depth - i - 1));
				c <<= 1;
			}
			return l;
		}

		template<class Fn>
		size_t enumerate(size_t l, size_t r, Fn&& fn) const
		{
			return enumerate(0, 0, l, r, 0, std::forward<Fn>(fn));
		}
	};
}
