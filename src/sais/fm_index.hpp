#include <algorithm>
#include <map>

#include "sais.hpp"
#include "wavelet_tree.hpp"

namespace sais
{
	template<class ChrTy>
	class FmIndex
	{
		std::unique_ptr<ChrTy[]> bwtData;
		//std::unique_ptr<size_t[]> freqs;
		std::unique_ptr<ChrTy[]> cKeys;
		std::unique_ptr<size_t[]> cValues;
		size_t length = 0, vocabSize = 0;
		WaveletTree waveletTree;
	public:
		FmIndex() = default;

		FmIndex(const ChrTy* data, size_t _length, mp::ThreadPool* pool = nullptr)
			: length{ _length }
		{
			bwtData = std::unique_ptr<ChrTy[]>(new ChrTy[length]);
			if (length < 0x80000000)
			{
				auto ibuf = std::unique_ptr<int32_t[]>(new int32_t[length + 1]);
				bwt<char16_t, int32_t>(data, bwtData.get(), ibuf.get(), length, 0, nullptr, pool);
			}
			else
			{
				auto ibuf = std::unique_ptr<int64_t[]>(new int64_t[length + 1]);
				bwt<char16_t, int64_t>(data, bwtData.get(), ibuf.get(), length, 0, nullptr, pool);
			}
			waveletTree = WaveletTree{ bwtData.get(), length };

			/*freqs = std::unique_ptr<size_t[]>(new size_t[(size_t)1 << (sizeof(ChrTy) * 8)]);
			std::fill(freqs.get(), freqs.get() + ((size_t)1 << (sizeof(ChrTy) * 8)), 0);
			for (size_t i = 0; i < length; ++i)
			{
				freqs[data[i]]++;
			}*/

			std::map<ChrTy, size_t> chrFreqs;
			for (size_t i = 0; i < length; ++i)
			{
				chrFreqs[data[i]]++;
			}
			vocabSize = chrFreqs.size();
			cKeys = std::unique_ptr<ChrTy[]>(new ChrTy[vocabSize]);
			cValues = std::unique_ptr<size_t[]>(new size_t[vocabSize]);
			size_t idx = 0, acc = 0;
			for (auto& p : chrFreqs)
			{
				cKeys[idx] = p.first;
				cValues[idx] = acc;
				acc += p.second;
				++idx;
			}
		}

		size_t size() const
		{
			return length;
		}

		std::pair<size_t, size_t> initRange(ChrTy startChr) const
		{
			auto it = std::lower_bound(cKeys.get(), cKeys.get() + vocabSize, startChr);
			if (it == cKeys.get() + vocabSize || *it != startChr) return std::make_pair(0, 0);
			size_t b = cValues[it - cKeys.get()];
			size_t e = (it + 1 < cKeys.get() + vocabSize) ? cValues[it + 1 - cKeys.get()] : length;
			return std::make_pair(b, e);
		}

		std::pair<size_t, size_t> nextRange(const std::pair<size_t, size_t>& range, ChrTy nextChr) const
		{
			auto it = std::lower_bound(cKeys.get(), cKeys.get() + vocabSize, nextChr);
			if (it == cKeys.get() + vocabSize || *it != nextChr) return std::make_pair(0, 0);
			size_t b = cValues[it - cKeys.get()];
			size_t e = b;
			b += waveletTree.rank(nextChr, range.first);
			e += waveletTree.rank(nextChr, range.second);
			if (b > e) return std::make_pair(0, 0);
			return std::make_pair(b, e);
		}

		using SuffixTy = std::basic_string<ChrTy>;
		using TraceTy = std::vector<std::pair<size_t, size_t>>;

		template<class Fn>
		size_t enumSuffices(size_t minCnt, SuffixTy& suffix, TraceTy& trace, size_t l, size_t r, Fn&& fn) const
		{
			size_t ret = 0;
			waveletTree.enumerate(l, r, [&](ChrTy c, size_t cl, size_t cr)
			{
				if (cr - cl < minCnt) return;
				auto it = std::lower_bound(cKeys.get(), cKeys.get() + vocabSize, c);
				auto b = cValues[it - cKeys.get()];
				suffix.push_back(c);
				trace.emplace_back(b + cl, b + cr);
				if (!fn(const_cast<const SuffixTy&>(suffix), const_cast<const TraceTy&>(trace)))
				{
					suffix.pop_back();
					trace.pop_back();
					return;
				}
				ret++;
				ret += enumSuffices(minCnt, suffix, trace, b + cl, b + cr, fn);
				suffix.pop_back();
				trace.pop_back();
			});
			return ret;
		}

		template<class Fn>
		size_t enumSuffices(size_t minCnt, Fn&& fn) const
		{
			SuffixTy suffix;
			TraceTy trace;
			size_t numSuffices = 0;
			for (size_t k = 0; k < vocabSize; ++k)
			{
				auto p = std::make_pair(cValues[k], (k + 1 < vocabSize) ? cValues[k + 1] : length);
				if (p.second - p.first < minCnt) continue;
				suffix.push_back(cKeys[k]);
				trace.emplace_back(p);
				if (!fn(const_cast<const SuffixTy&>(suffix), const_cast<const TraceTy&>(trace)))
				{
					suffix.pop_back();
					trace.pop_back();
					continue;
				}
				numSuffices++;

				numSuffices += enumSuffices(minCnt, suffix, trace, p.first, p.second, fn);
				suffix.pop_back();
				trace.pop_back();
			}
			return numSuffices;
		}
	};
}
