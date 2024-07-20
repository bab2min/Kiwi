#include <algorithm>

#include <kiwi/Utils.h>
#include <kiwi/Types.h>
#include <kiwi/Trie.hpp>
#include <kiwi/SubstringExtractor.h>

#include "StrUtils.h"
#include "FrozenTrie.hpp"
#include "Knlm.hpp"

#include "sais/fm_index.hpp"


#ifdef KIWI_USE_BTREE

#ifdef _WIN32
using ssize_t = ptrdiff_t;
#else
#include <sys/types.h>
#endif

#include <btree/map.h>
#else
#endif


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
			auto u32size = s.size();
			for (size_t i = 0; i < s.size(); ++i)
			{
				if (isLowSurrogate(s[i])) u32size--;
			}

			if (u32size > maxLength) return false;

			if (find(s.begin(), s.end(), stopChr) != s.end())
			{
				return false;
			}

			if (isHighSurrogate(s.front())) return false;
			if (isLowSurrogate(s.back())) return true;

			if (testRepetition(s.data(), s.size()))
			{
				return false;
			}

			if (u32size < minLength)
			{
				return true;
			}

			const auto ssLength = s.size();
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

#ifdef KIWI_USE_BTREE
	template<typename K, typename V> using map = btree::map<K, V>;
#else
	template<typename K, typename V> using map = std::map<K, V>;
#endif

	template<class IntTy>
	using PrefixTrieNode = utils::TrieNodeEx<IntTy, uint32_t, utils::ConstAccess<map<IntTy, int32_t>>>;

	PrefixCounter::PrefixCounter(size_t _prefixSize, size_t _minCf, size_t _numWorkers)
		: prefixSize(_prefixSize), minCf(_minCf), id2Token(2), buf(1)
	{
		if (_numWorkers == 0) _numWorkers = min(thread::hardware_concurrency(), 8u);
		if (_numWorkers > 1)
		{
			threadPool = make_unique<mp::ThreadPool>(_numWorkers);
		}
	}

	template<class It>
	void PrefixCounter::_addArray(It first, It last)
	{
		for (; first != last; ++first)
		{
			const auto token = *first;
			auto it = token2id.find(token);
			if (it == token2id.end())
			{
				const auto id = id2Token.size();
				it = token2id.emplace(token, id).first;
				id2Token.push_back(token);
			}
			if (it->second < 0x4000)
			{
				buf.emplace_back(it->second);
			}
			else if (it->second < 0x10000000)
			{
				buf.emplace_back((it->second & 0x3FFF) | 0x4000);
				buf.emplace_back((it->second >> 14) | 0x8000);
			}
			else
			{
				throw runtime_error("Too many tokens");
			}
		}
		buf.emplace_back(1);
		numArrays += 1;
	}


	void PrefixCounter::addArray(const uint16_t* first, const uint16_t* last)
	{
		_addArray(first, last);
	}

	void PrefixCounter::addArray(const uint32_t* first, const uint32_t* last)
	{
		_addArray(first, last);
	}

	void PrefixCounter::addArray(const uint64_t* first, const uint64_t* last)
	{
		_addArray(first, last);
	}

	utils::FrozenTrie<uint32_t, uint32_t> PrefixCounter::count() const
	{
		sais::FmIndex<char16_t> fi{ (const char16_t*)buf.data(), buf.size(), (mp::ThreadPool*)threadPool.get()};
		utils::ContinuousTrie<PrefixTrieNode<uint32_t>> trie{ 1 };
		trie.root().val = buf.size() - 1 - numArrays;
		
		unique_ptr<mutex> mtx;
		if (threadPool)
		{
			mtx = make_unique<mutex>();
		}

		fi.enumSuffices(minCf, [&](const sais::FmIndex<char16_t>::SuffixTy& s, const sais::FmIndex<char16_t>::TraceTy& t)
		{
			auto u32size = s.size();
			for (size_t i = 0; i < s.size(); ++i)
			{
				if (s[i] & 0x8000)
				{
					u32size--;
				}
			}

			if (u32size > prefixSize) return false;

			if (find(s.begin(), s.end(), 0) != s.end() || find(s.begin(), s.end(), 1) != s.end())
			{
				return false;
			}

			if (s.front() & 0x4000) return false;
			if (s.back() & 0x8000) return true;

			const auto suffixCnt = t.back().second - t.back().first;
			if (suffixCnt < minCf)
			{
				return false;
			}

			thread_local Vector<uint32_t> restoredBuf;
			restoredBuf.clear();
			for(auto rit = s.rbegin(); rit != s.rend(); ++rit)
			{
				if (*rit & 0x4000)
				{
					const auto merged = (rit[0] & 0x3FFF) | ((rit[1] & 0x3FFF) << 14);
					restoredBuf.push_back(id2Token[merged]);
					++rit;
				}
				else if (*rit & 0x8000)
				{
					throw runtime_error("Invalid token");
				}
				else
				{
					restoredBuf.push_back(id2Token[*rit]);
				}
			}
			mp::OptionalLockGuard<mutex> lock{ mtx.get() };
			trie.build(restoredBuf.begin(), restoredBuf.end(), suffixCnt);
			return true;
		}, (mp::ThreadPool*)threadPool.get());
		return utils::freezeTrie(move(trie), ArchType::balanced);
	}

	unique_ptr<lm::KnLangModelBase> PrefixCounter::buildLM(
		size_t lastMinCf, 
		size_t bosTokenId,
		size_t eosTokenId,
		size_t unkTokenId,
		ArchType archType) const
	{
		utils::MemoryOwner mem;
		{
			auto trie = count();
			mem = lm::KnLangModelBase::build(move(trie), prefixSize, minCf, lastMinCf, unkTokenId, bosTokenId, eosTokenId, 1e-5f, 0, false);
		}
		return lm::KnLangModelBase::create(move(mem), archType);
	}
}
