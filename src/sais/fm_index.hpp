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
		WaveletTree<ChrTy> waveletTree;
	public:
		FmIndex() = default;

		FmIndex(const ChrTy* data, size_t _length, mp::ThreadPool* pool = nullptr)
			: length{ _length }
		{
			bwtData = std::unique_ptr<ChrTy[]>(new ChrTy[length]);
			if (length < 0x80000000)
			{
				auto ibuf = std::unique_ptr<int32_t[]>(new int32_t[length + 1]);
				bwt<ChrTy, int32_t>(data, bwtData.get(), ibuf.get(), length, 0, nullptr, pool);
			}
			else
			{
				auto ibuf = std::unique_ptr<int64_t[]>(new int64_t[length + 1]);
				bwt<ChrTy, int64_t>(data, bwtData.get(), ibuf.get(), length, 0, nullptr, pool);
			}
			waveletTree = WaveletTree<ChrTy>{ bwtData.get(), length };

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

		template<class It>
		std::pair<size_t, size_t> findRange(It first, It last) const
		{
			if (first == last) return std::make_pair(0, 0);

			std::pair<size_t, size_t> range = initRange(*first);
			if (range.first == 0 && range.second == 0)
			{
				return range;
			}
			++first;
			for (; first != last; ++first)
			{
				range = nextRange(range, *first);
				if (range.first == 0 && range.second == 0)
				{
					return range;
				}
			}
			return range;
		}

		template<class It, class Alloc>
		bool findTrace(std::vector<size_t, Alloc>& out, It first, It last) const
		{
			out.clear();
			if (first == last) return false;
			std::pair<size_t, size_t> range = initRange(*first);
			++first;
			for (; first != last; ++first)
			{
				auto nextChr = *first;
				auto it = std::lower_bound(cKeys.get(), cKeys.get() + vocabSize, nextChr);
				if (it == cKeys.get() + vocabSize || *it != nextChr) return false;
				const size_t b = cValues[it - cKeys.get()];
				const size_t ob = waveletTree.rank(nextChr, range.first);
				const size_t oe = waveletTree.rank(nextChr, range.second);
				if (ob > oe) return false;
				
				const size_t rSize = range.second - range.first;
				const size_t numHistories = out.size() / rSize;
				size_t validIdx = 0;
				for (size_t h = 0; h < numHistories; ++h)
				{
					for (size_t i = range.first; i < range.second; ++i)
					{
						if (bwtData[i] == nextChr)
						{
							out[validIdx] = out[h * rSize + i - range.first];
							validIdx++;
						}
					}
				}
				out.resize(validIdx);

				for (size_t i = range.first; i < range.second; ++i)
				{
					if (bwtData[i] == nextChr)
					{
						out.emplace_back(i);
					}
				}
				
				range = std::make_pair(b + ob, b + oe);
			}
			for (size_t i = range.first; i < range.second; ++i)
			{
				out.emplace_back(i);
			}
			return true;
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

		template<class It, class Fn>
		size_t enumSufficesOfString(size_t minCnt, It first, It last, Fn&& fn) const
		{
			std::pair<size_t, size_t> range = findRange(first, last);
			if (range.first == 0 && range.second == 0)
			{
				return 0;
			}
			SuffixTy suffix;
			TraceTy trace;
			return enumSuffices(minCnt, suffix, trace, range.first, range.second, std::move(fn));
		}

		template<class Fn>
		size_t enumSuffices(size_t minCnt, Fn&& fn, mp::ThreadPool* tp = nullptr) const
		{
			auto numSuffices = mp::runParallel(tp, [&](const size_t i, const size_t numWorkers, mp::Barrier*)
			{
				SuffixTy suffix;
				TraceTy trace;
				size_t numSuffices = 0;
				for (size_t k = i; k < vocabSize; k += numWorkers)
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
			});
			return std::accumulate(numSuffices.begin(), numSuffices.end(), (size_t)0);
		}
	};
}
