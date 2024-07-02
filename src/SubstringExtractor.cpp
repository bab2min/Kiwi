#include <algorithm>

#include <kiwi/Types.h>
#include <kiwi/SubstringExtractor.h>

#include "StrUtils.h"

#include "sais/fm_index.hpp"

using namespace std;

namespace kiwi
{
	inline bool testRepetition(const char16_t* s, size_t l)
	{
		if (l < 5) return false;
		for (size_t i = 1; i <= l / 3; ++i)
		{
			bool all = true;
			for (size_t j = 1; j < l / i; ++j)
			{
				if (!equal(s, s + i, s + i * j))
				{
					all = false;
					break;
				}
			}
			if (all) return true;
		}
		return false;
	}

	vector<pair<u16string, size_t>> extractSubstrings(
		const char16_t* first, 
		const char16_t* last, 
		size_t minCnt, 
		size_t minLength,
		size_t maxLength,
		bool longestOnly,
		char16_t stopChr
	)
	{
		Vector<char16_t> buf(last - first + 1);
		copy(first, last, buf.begin() + 1);
		sais::FmIndex<char16_t> fi{ buf.data(), buf.size() };
		Vector<UnorderedMap<u16string, size_t>> candCnts;
		vector<pair<u16string, size_t>> ret;
		fi.enumSuffices(minCnt, [&](const sais::FmIndex<char16_t>::SuffixTy& s, const sais::FmIndex<char16_t>::TraceTy& t)
		{
			if (s.size() > maxLength) return false;

			if (find(s.begin(), s.end(), stopChr) != s.end())
			{
				return false;
			}

			if (isLowSurrogate(s.back()) || isHighSurrogate(s.front())) return false;

			if (testRepetition(s.data(), s.size()))
			{
				return false;
			}

			const auto ssLength = s.size();
			if (ssLength < minLength)
			{
				return true;
			}

			const auto ssCnt = t.back().second - t.back().first;
			if (ssCnt < minCnt)
			{
				return true;
			}

			if (longestOnly)
			{
				if (candCnts.size() <= ssLength - minLength) candCnts.resize(ssLength - minLength + 1);
				candCnts[ssLength - minLength].emplace(u16string{ s.rbegin(), s.rend() }, ssCnt);
			}
			else
			{
				ret.emplace_back(u16string{ s.rbegin(), s.rend() }, ssCnt);
			}
			return true;
		});

		if (longestOnly)
		{
			for (size_t i = 1; i < candCnts.size(); ++i)
			{
				auto& cands = candCnts[i];
				auto& subCands = candCnts[i - 1];
				for (auto& p : cands)
				{
					auto it = subCands.find(p.first.substr(1));
					if (it != subCands.end() && it->second == p.second)
					{
						subCands.erase(it);
					}

					it = subCands.find(p.first.substr(0, p.first.size() - 1));
					if (it != subCands.end() && it->second == p.second)
					{
						subCands.erase(it);
					}
				}
			}

			for (auto it = candCnts.rbegin(); it != candCnts.rend(); ++it)
			{
				size_t offset = ret.size();
				for (auto& p : *it)
				{
					ret.emplace_back(p.first, p.second);
				}
				sort(ret.begin() + offset, ret.end(), [&](const pair<u16string, size_t>& a, const pair<u16string, size_t>& b)
				{
					return a.second > b.second;
				});
			}
		}
		else
		{
			sort(ret.begin(), ret.end(), [&](const pair<u16string, size_t>& a, const pair<u16string, size_t>& b)
			{
				if (a.first.size() > b.first.size())
				{
					return true;
				}
				else if (a.first.size() < b.first.size())
				{
					return false;
				}
				return a.second > b.second;
			});
		}

		return ret;
	}
}
