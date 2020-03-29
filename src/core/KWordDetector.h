#pragma once
#include "KForm.h"
#include "ThreadPool.h"

namespace std
{
	template<>
	struct hash<pair<uint16_t, uint16_t>>
	{
		size_t operator()(const pair<uint16_t, uint16_t>& o) const
		{
			return hash<uint16_t>{}(o.first) | (hash<uint16_t>{}(o.second) << 16);
		}
	};
}

namespace kiwi
{
	template<class KeyType = char16_t, class ValueType = uint32_t>
	class WordDictionary
	{
	protected:
		std::map<KeyType, ValueType> word2id;
		std::vector<KeyType> id2word;
		std::mutex mtx;
	public:

		WordDictionary() {}
		WordDictionary(const WordDictionary& o) : word2id(o.word2id), id2word(o.id2word) {}
		WordDictionary(WordDictionary&& o)
		{
			std::swap(word2id, o.word2id);
			std::swap(id2word, o.id2word);
		}

		WordDictionary& operator=(WordDictionary&& o)
		{
			std::swap(word2id, o.word2id);
			std::swap(id2word, o.id2word);
			return *this;
		}

		enum { npos = (ValueType)-1 };

		ValueType add(const KeyType& str)
		{
			if (word2id.emplace(str, word2id.size()).second) id2word.emplace_back(str);
			return word2id.size() - 1;
		}

		ValueType getOrAdd(const KeyType& str)
		{
			std::lock_guard<std::mutex> lg(mtx);
			auto it = word2id.find(str);
			if (it != word2id.end()) return it->second;
			return add(str);
		}

		template<class Iter>
		std::vector<ValueType> getOrAdds(Iter begin, Iter end)
		{
			std::lock_guard<std::mutex> lg(mtx);
			return getOrAddsWithoutLock(begin, end);
		}

		template<class Iter>
		std::vector<ValueType> getOrAddsWithoutLock(Iter begin, Iter end)
		{
			std::vector<ValueType> ret;
			for (; begin != end; ++begin)
			{
				auto it = word2id.find(*begin);
				if (it != word2id.end()) ret.emplace_back(it->second);
				else ret.emplace_back(add(*begin));
			}
			return ret;
		}

		template<class Func>
		void withLock(const Func& f)
		{
			std::lock_guard<std::mutex> lg(mtx);
			f();
		}

		ValueType get(const KeyType& str) const
		{
			auto it = word2id.find(str);
			if (it != word2id.end()) return it->second;
			return npos;
		}

		const KeyType& getStr(ValueType id) const
		{
			return id2word[id];
		}

		size_t size() const { return id2word.size(); }
	};

	class u16light
	{
	private:
		static constexpr size_t dlen = sizeof(char16_t*) - 1;
		union
		{
			struct
			{
				size_t len;
				char16_t* data;
			};
			struct
			{
				uint16_t rawLen;
				std::array<char16_t, dlen> rawData;
			};
		};
	public:
		u16light() : rawLen(0), rawData({})
		{
		}

		u16light(const u16light& o)
		{
			if (o.rawLen <= dlen)
			{
				rawLen = o.rawLen;
				rawData = o.rawData;
			}
			else
			{
				len = o.len;
				data = new char16_t[len];
				memcpy(data, o.data, len * sizeof(char16_t));
			}
		}

		u16light(u16light&& o) : rawLen(0), rawData({})
		{
			swap(o);
		}

		template<class Iter>
		u16light(Iter begin, Iter end)
		{
			size_t l = std::distance(begin, end);
			if (l <= dlen)
			{
				rawLen = l;
				std::copy(begin, end, rawData.begin());
			}
			else
			{
				len = l;
				data = new char16_t[l];
				std::copy(begin, end, data);
			}
		}

		~u16light()
		{
			if (rawLen > dlen) delete[] data;
		}

		u16light& operator=(const u16light& o)
		{
			if (rawLen > dlen) delete[] data;

			if (o.rawLen <= dlen)
			{
				rawLen = o.rawLen;
				rawData = o.rawData;
			}
			else
			{
				len = o.len;
				data = new char16_t[len];
				memcpy(data, o.data, len * sizeof(char16_t));
			}
			return *this;
		}

		u16light& operator=(u16light&& o)
		{
			swap(o);
			return *this;
		}

		bool operator<(const u16light& o) const
		{
			return std::lexicographical_compare(begin(), end(), o.begin(), o.end());
		}

		void swap(u16light& o)
		{
			std::swap(rawLen, o.rawLen);
			std::swap(rawData, o.rawData);
		}

		size_t size() const
		{
			return rawLen;
		}

		bool empty() const
		{
			return !rawLen;
		}

		const char16_t* begin() const
		{
			if (rawLen <= dlen) return &rawData[0];
			else return data;
		}

		const char16_t* end() const
		{
			if (rawLen <= dlen) return &rawData[0] + rawLen;
			else return data + len;
		}

		std::reverse_iterator<const char16_t*> rbegin() const
		{
			return std::reverse_iterator<const char16_t*>(end());
		}

		std::reverse_iterator<const char16_t*> rend() const
		{
			return std::reverse_iterator<const char16_t*>(begin());
		}

		char16_t& front()
		{
			if (rawLen <= dlen) return rawData[0];
			else return data[0];
		}

		char16_t& back()
		{
			if (rawLen <= dlen) return rawData[rawLen - 1];
			else return data[len - 1];
		}

		const char16_t& front() const
		{
			if (rawLen <= dlen) return rawData[0];
			else return data[0];
		}

		const char16_t& back() const
		{
			if (rawLen <= dlen) return rawData[rawLen - 1];
			else return data[len - 1];
		}

		bool startsWith(const u16light& o) const
		{
			if (o.size() > size()) return false;
			return std::equal(o.begin(), o.end(), begin());
		}
	};

	class KWordDetector
	{
		struct Counter
		{
			WordDictionary<char16_t, uint16_t> chrDict;
			std::vector<uint32_t> cntUnigram;
			std::unordered_set<std::pair<uint16_t, uint16_t>> candBigram;
			std::map<u16light, uint32_t> forwardCnt, backwardCnt;
		};
	protected:
		size_t minCnt, maxWordLen;
		float minScore;
		size_t numThread;
		std::map<std::pair<KPOSTag, bool>, std::map<char16_t, float>> posScore;
		std::map<std::u16string, float> nounTailScore;

		template<class LocalData, class FuncReader, class FuncProc>
		std::vector<LocalData> readProc(const FuncReader& reader, const FuncProc& processor, LocalData&& ld = {}) const
		{
			ThreadPool workers{ numThread, numThread * 2 };
			std::vector<LocalData> ldByTid(workers.getNumWorkers(), ld);
			for (size_t id = 0; ; ++id)
			{
				auto ustr = reader(id);
				if (ustr.empty()) break;
				workers.enqueue([this, ustr, id, &ldByTid, &processor](size_t tid)
				{
					auto& ld = ldByTid[tid];
					processor(ustr, id, ld);
				});
			}
			return ldByTid;
		}
		void countUnigram(Counter&, const std::function<std::u16string(size_t)>& reader) const;
		void countBigram(Counter&, const std::function<std::u16string(size_t)>& reader) const;
		void countNgram(Counter&, const std::function<std::u16string(size_t)>& reader) const;
		float branchingEntropy(const std::map<u16light, uint32_t>& cnt, std::map<u16light, uint32_t>::iterator it, float defaultPerp = 1.f) const;
		std::map<KPOSTag, float> getPosScore(Counter&, const std::map<u16light, uint32_t>& cnt, std::map<u16light, uint32_t>::iterator it, bool coda, const std::u16string& realForm) const;
	public:

		struct WordInfo
		{
			std::u16string form;
			float score, lBranch, rBranch, lCohesion, rCohesion;
			uint32_t freq;
			std::map<KPOSTag, float> posScore;

			WordInfo(std::u16string _form = {},
				float _score = 0, float _lBranch = 0, float _rBranch = 0,
				float _lCohesion = 0, float _rCohesion = 0, uint32_t _freq = 0,
				std::map<KPOSTag, float>&& _posScore = {})
				: form(_form), score(_score), lBranch(_lBranch), rBranch(_rBranch),
				lCohesion(_lCohesion), rCohesion(_rCohesion), freq(_freq), posScore(_posScore)
			{}
		};

		KWordDetector(size_t _minCnt = 10, size_t _maxWordLen = 10, float _minScore = 0.1f,
			size_t _numThread = 0)
			: minCnt(_minCnt), maxWordLen(_maxWordLen), minScore(_minScore),
			numThread(_numThread ? _numThread : std::thread::hardware_concurrency())
		{}
		void setParameters(size_t _minCnt = 10, size_t _maxWordLen = 10, float _minScore = 0.1f)
		{
			minCnt = _minCnt;
			maxWordLen = _maxWordLen;
			minScore = _minScore;
		}
		void loadPOSModelFromTxt(std::istream& is);
		void loadNounTailModelFromTxt(std::istream& is);
		void loadPOSModel(std::istream& is);
		void loadNounTailModel(std::istream& is);
		void savePOSModel(std::ostream& os);
		void saveNounTailModel(std::ostream& os);
		std::vector<WordInfo> extractWords(const std::function<std::u16string(size_t)>& reader) const;
	};

}