#include <kiwi/BpeTokenizer.h>
#include <kiwi/ThreadPool.h>
#include <kiwi/Utils.h>
#include "StrUtils.h"
#include "UnicodeCase.h"

#include <algorithm>
#include <cassert>
#include <queue>
#include <set>
#include <cctype>
#include <climits>
#include <ostream>
#include <istream>
#include <nlohmann/json.hpp>

using namespace std;

namespace kiwi
{
	// -------------------------------------------------------------------------
	// StringArena
	//
	// Bump-pointer allocator for interning short UTF-8 strings.  Strings are
	// written sequentially into a single vector<char>; no per-string heap
	// allocation.  Callers address strings via packed uint64_t handles rather
	// than raw pointers, so vector reallocation on grow does not invalidate
	// any stored references.
	// -------------------------------------------------------------------------
	class StringArena
	{
		vector<char> data;

	public:
		// For largeCounter=false: stores with NUL terminator, returns uint32_t offset.
		// Arena size is limited to 4 GB.
		uint32_t intern(string_view sv)
		{
			assert(!sv.empty());
			assert(data.size() + sv.size() + 1 <= UINT32_MAX && "StringArena exceeds 4 GB");
			const uint32_t offset = static_cast<uint32_t>(data.size());
			data.insert(data.end(), sv.begin(), sv.end());
			data.push_back('\0');
			return offset;
		}

		const char* ptr(uint32_t offset) const { return data.data() + offset; }

		// For largeCounter=true: stores without NUL, returns packed handle.
		//   bits [63:24] = byte offset (40 bits, up to ~1 TB)
		//   bits [23: 0] = length      (24 bits, up to 16 MB)
		uint64_t internPacked(string_view sv)
		{
			assert(!sv.empty() && sv.size() <= 0xFFFFFF);
			const uint64_t offset = static_cast<uint64_t>(data.size());
			assert(offset <= 0xFFFFFFFFFFull && "StringArena offset overflow (> 1 TB)");
			data.insert(data.end(), sv.begin(), sv.end());
			return (offset << 24) | static_cast<uint64_t>(sv.size());
		}

		string_view view(uint64_t packed) const
		{
			return { data.data() + (packed >> 24), packed & 0xFFFFFF };
		}

		// Resets content but keeps allocated capacity for reuse across batches.
		void clear() { data.clear(); }
	};

	// -------------------------------------------------------------------------
	// WordCountMap<largeCounter>  –  open-addressing hash map.
	//
	// largeCounter=false: 8-byte slots  {uint32_t offset, uint32_t count}
	//   Key = NUL-terminated string at arena[offset].  Sentinel: offset==UINT32_MAX.
	//   Arena size limit: 4 GB.  Count limit: ~4 billion.
	//
	// largeCounter=true: 16-byte slots  {uint64_t packed, uint64_t count}
	//   Key = packed handle (bits[63:24]=offset 40b, bits[23:0]=length 24b).
	//   No NUL in arena.  Sentinel: packed==0 (BPE chunks are never empty).
	//   Arena size limit: ~1 TB.  Count limit: ~1.8e19.
	// -------------------------------------------------------------------------
	template<bool largeCounter>
	class WordCountMap
	{
		using KeyT   = conditional_t<largeCounter, uint64_t, uint32_t>;
		using CountT = conditional_t<largeCounter, uint64_t, uint32_t>;
		struct Slot { KeyT key; CountT count; }; // 8B or 16B
		static constexpr KeyT emptyKey = largeCounter ? KeyT(0) : KeyT(UINT32_MAX);

		vector<Slot> slots;
		size_t used = 0;
		StringArena* arena = nullptr;

		static size_t fnv1a(string_view sv) noexcept
		{
			size_t h = 14695981039346656037ull;
			for (unsigned char c : sv) h = (h ^ c) * 1099511628211ull;
			return h;
		}

		string_view slotView(const Slot& s) const
		{
			if constexpr (largeCounter)
				return arena->view(s.key);
			else
			{
				const char* p = arena->ptr(s.key);
				return { p, strlen(p) };
			}
		}

		void grow()
		{
			const size_t newCap = slots.empty() ? 64 : slots.size() * 2;
			vector<Slot> newSlots(newCap, Slot{ emptyKey, 0 });
			const size_t mask = newCap - 1;
			for (auto& s : slots)
			{
				if (s.key == emptyKey) continue;
				size_t idx = fnv1a(slotView(s)) & mask;
				while (newSlots[idx].key != emptyKey) idx = (idx + 1) & mask;
				newSlots[idx] = s;
			}
			slots = move(newSlots);
		}

	public:
		explicit WordCountMap(StringArena* a) : arena(a) {}

		void add(string_view sv, size_t delta = 1)
		{
			if (used * 5 >= slots.size() * 3) grow(); // 60% load factor

			const size_t mask = slots.size() - 1;
			size_t idx = fnv1a(sv) & mask;
			while (slots[idx].key != emptyKey)
			{
				if (slotView(slots[idx]) == sv)
				{
					slots[idx].count += static_cast<CountT>(delta);
					return;
				}
				idx = (idx + 1) & mask;
			}
			if constexpr (largeCounter)
				slots[idx] = { arena->internPacked(sv), static_cast<CountT>(delta) };
			else
				slots[idx] = { arena->intern(sv), static_cast<CountT>(delta) };
			++used;
		}

		void clear()
		{
			fill(slots.begin(), slots.end(), Slot{ emptyKey, 0 });
			used = 0;
		}

		size_t size() const { return used; }

		// Callback: f(string_view, size_t count)
		template<class F>
		void forEach(F&& f) const
		{
			for (auto& s : slots)
				if (s.key != emptyKey) f(slotView(s), static_cast<size_t>(s.count));
		}
	};

	// -------------------------------------------------------------------------
	// WordCounts  –  arena + map kept together.
	// arena must be declared before map so the pointer passed to map is valid.
	// -------------------------------------------------------------------------
	template<bool largeCounter>
	struct WordCountsT
	{
		StringArena arena;
		WordCountMap<largeCounter> map;

		WordCountsT() : map(&arena) {}

		void add(string_view sv, size_t delta = 1) { map.add(sv, delta); }
		void clear()                               { arena.clear(); map.clear(); }
		size_t size() const                        { return map.size(); }

		// Callback: f(string_view sv, size_t count)
		template<class F> void forEach(F&& f) const { map.forEach(forward<F>(f)); }
	};

	// BpeTokenizerTrainer::Impl  –  virtual interface.
	// Concrete type is BpeTokenizerTrainerImpl<largeCounter> defined below,
	// after the UTF-8 / chunk-extraction helpers it depends on.
	struct BpeTokenizerTrainer::Impl
	{
		virtual ~Impl() = default;
		virtual size_t addSentences(size_t& sentenceCount, const BpeTrainerConfig& config,
		                            const function<string()>& feeder) = 0;
		virtual BpeTokenizer build(const BpeTrainerConfig& config) const = 0;
	};

	// =========================================================================
	// UTF-8 helpers and chunk extraction (unchanged)
	// =========================================================================

	struct Utf8Codepoint
	{
		char32_t value;
		size_t size;
	};

	// Invalid UTF-8 bytes are kept as individual non-letter/non-number bytes so
	// byte-level tokenization remains lossless for arbitrary std::string input.
	static Utf8Codepoint decodeUtf8Codepoint(const string& str, size_t pos)
	{
		const auto byte = static_cast<unsigned char>(str[pos]);
		if (byte < 0x80) return { byte, 1 };

		size_t length = 0;
		char32_t code = 0;
		char32_t minimum = 0;
		if ((byte & 0xE0) == 0xC0) { length = 2; code = byte & 0x1F; minimum = 0x80; }
		else if ((byte & 0xF0) == 0xE0) { length = 3; code = byte & 0x0F; minimum = 0x800; }
		else if ((byte & 0xF8) == 0xF0) { length = 4; code = byte & 0x07; minimum = 0x10000; }
		else return { byte, 1 };

		if (pos + length > str.size()) return { byte, 1 };
		for (size_t i = 1; i < length; ++i)
		{
			const auto continuation = static_cast<unsigned char>(str[pos + i]);
			if ((continuation & 0xC0) != 0x80) return { byte, 1 };
			code = (code << 6) | (continuation & 0x3F);
		}
		if (code < minimum || code > 0x10FFFF || (0xD800 <= code && code <= 0xDFFF)) return { byte, 1 };
		return { code, length };
	}

	static vector<pair<size_t, size_t>> extractChunkSpans(const string& str)
	{
		vector<pair<size_t, size_t>> chunks;
		if (str.empty()) return chunks;

		size_t i = 0;
		size_t n = str.size();

		auto codepointAt = [&](size_t pos) { return decodeUtf8Codepoint(str, pos); };
		auto isLetter = [&](size_t pos) { return isUnicodeLetter(codepointAt(pos).value); };
		auto isNumber = [&](size_t pos) { return isUnicodeNumber(codepointAt(pos).value); };
		auto isSpace = [&](size_t pos) { return isUnicodeSpace(codepointAt(pos).value); };
		auto isOther = [&](size_t pos)
		{
			auto c = codepointAt(pos).value;
			return !isUnicodeLetter(c) && !isUnicodeNumber(c) && !isUnicodeSpace(c);
		};

		while (i < n)
		{
			size_t start = i;

			// 1. Contractions
			if (str[i] == '\'')
			{
				string lowerStr;
				for (size_t k = i; k < i + 4 && k < n; ++k) lowerStr.push_back(tolower((unsigned char)str[k]));

				if (lowerStr.substr(0, 2) == "'s" || lowerStr.substr(0, 2) == "'t" ||
					lowerStr.substr(0, 2) == "'m" || lowerStr.substr(0, 2) == "'d")
				{
					i += 2;
					chunks.push_back({start, i - start});
					continue;
				}
				else if (lowerStr.substr(0, 3) == "'re" || lowerStr.substr(0, 3) == "'ve" ||
						 lowerStr.substr(0, 3) == "'ll")
				{
					i += 3;
					chunks.push_back({start, i - start});
					continue;
				}
			}

			// 2. Spaces
			if (isSpace(i))
			{
				size_t j = i;
				size_t lastSpace = i;
				while (j < n && isSpace(j))
				{
					lastSpace = j;
					j += codepointAt(j).size;
				}

				if (j == n)
				{
					chunks.push_back({start, j - start});
					i = j;
					continue;
				}

				if (str[lastSpace] == ' ')
				{
					if (j - i > 1)
					{
						chunks.push_back({start, (j - 1) - start});
						i = j - 1;
						continue;
					}
				}
				else
				{
					chunks.push_back({start, j - start});
					i = j;
					continue;
				}
			}

			// 3. Optional space followed by Letter, Number, or Other
			if (i < n && str[i] == ' ')
			{
				i++;
			}

			if (i < n)
			{
				if (isLetter(i))
				{
					while (i < n && isLetter(i)) i += codepointAt(i).size;
				}
				else if (isNumber(i))
				{
					while (i < n && isNumber(i)) i += codepointAt(i).size;
				}
				else if (!isSpace(i))
				{
					while (i < n && isOther(i)) i += codepointAt(i).size;
				}
			}

			chunks.push_back({start, i - start});
		}
		return chunks;
	}

	static size_t getWorkerCount(size_t configuredThreads)
	{
		size_t n = configuredThreads;
		if (!n) n = thread::hardware_concurrency();
		if (!n) n = 1;
		return n;
	}

	// Concrete Impl templated on largeCounter.
	// All hot-path code (chunk extraction, counting, merging) lives here and
	// operates directly on WordCountsT<LC> — no virtual dispatch in tight loops.
	template<bool largeCounter>
	struct BpeTokenizerTrainerImpl final : BpeTokenizerTrainer::Impl
	{
		WordCountsT<largeCounter> wordCounts;

		// ---- chunk extraction --------------------------------------------------

		static void addChunksTo(const string& str, bool addPrefixSpace,
		                        WordCountsT<largeCounter>& wc)
		{
			string prefixedStr;
			const string* workStr = &str;
			if (addPrefixSpace && !str.empty() && str[0] != ' ')
			{
				prefixedStr.reserve(str.size() + 1);
				prefixedStr = " ";
				prefixedStr += str;
				workStr = &prefixedStr;
			}
			for (auto& span : extractChunkSpans(*workStr))
				wc.add(string_view(workStr->data() + span.first, span.second));
		}

		// ---- addSentences ------------------------------------------------------

		size_t addSentences(size_t& sentenceCount, const BpeTrainerConfig& config,
		                    const function<string()>& feeder) override
		{
			size_t totalCount = 0;
			vector<string> batch;
			batch.reserve(config.batchSize);
			const size_t maxWorkerCount = getWorkerCount(config.numThreads);
			unique_ptr<utils::ThreadPool> pool;
			if (maxWorkerCount > 1)
				pool = make_unique<utils::ThreadPool>(maxWorkerCount);

			// Per-worker accumulators: allocated once, reused across batches.
			vector<WordCountsT<largeCounter>> localCounts(maxWorkerCount);

			while (true)
			{
				batch.clear();
				for (size_t i = 0; i < config.batchSize; ++i)
				{
					string sentence = feeder();
					if (sentence.empty()) break;
					batch.emplace_back(move(sentence));
				}
				if (batch.empty()) break;

				const size_t workerCount = min(maxWorkerCount, batch.size());
				for (size_t i = 0; i < workerCount; ++i) localCounts[i].clear();

				auto processRange = [&](size_t workerIndex)
				{
					const size_t begin = batch.size() * workerIndex / workerCount;
					const size_t end   = batch.size() * (workerIndex + 1) / workerCount;
					for (size_t i = begin; i < end; ++i)
						addChunksTo(batch[i], config.addPrefixSpace, localCounts[workerIndex]);
				};

				if (workerCount == 1)
				{
					processRange(0);
				}
				else
				{
					vector<future<void>> futures;
					futures.reserve(workerCount);
					for (size_t wi = 0; wi < workerCount; ++wi)
						futures.emplace_back(pool->enqueue([&, wi](size_t) { processRange(wi); }));
					for (auto& f : futures) f.get();
				}

				for (size_t i = 0; i < workerCount; ++i)
					localCounts[i].forEach([&](string_view sv, size_t count)
					{
						wordCounts.add(sv, count);
					});

				totalCount    += batch.size();
				sentenceCount += batch.size();
			}
			return totalCount;
		}

		// ---- build -------------------------------------------------------------

		BpeTokenizer build(const BpeTrainerConfig& config) const override
		{
			vector<string> vocab;
			vocab.reserve(min(config.vocabSize, (size_t)256));
			unordered_map<string, uint32_t> vocabToId;

			for (int i = 0; i < 256; ++i)
			{
				string s(1, (char)(unsigned char)i);
				vocab.push_back(s);
				vocabToId[s] = (uint32_t)i;
			}

			struct Word { vector<uint32_t> tokens; size_t count; };

			vector<Word> words;
			words.reserve(wordCounts.size());
			wordCounts.forEach([&](string_view sv, size_t count)
			{
				Word w;
				w.count = count;
				w.tokens.reserve(sv.size());
				for (unsigned char c : sv)
					w.tokens.push_back((uint32_t)c);
				words.push_back(move(w));
			});

			unordered_map<uint64_t, size_t> pairCounts;
			unordered_map<uint32_t, vector<size_t>> tokenToWords;

			auto makeKey = [](uint32_t a, uint32_t b) -> uint64_t {
				return ((uint64_t)a << 32) | (uint64_t)b;
			};

			for (size_t wIdx = 0; wIdx < words.size(); ++wIdx)
			{
				const auto& w = words[wIdx];
				for (size_t i = 0; i < w.tokens.size(); ++i)
				{
					tokenToWords[w.tokens[i]].push_back(wIdx);
					if (i + 1 < w.tokens.size())
						pairCounts[makeKey(w.tokens[i], w.tokens[i + 1])] += w.count;
				}
			}

			for (auto& kv : tokenToWords)
			{
				auto& vec = kv.second;
				sort(vec.begin(), vec.end());
				vec.erase(unique(vec.begin(), vec.end()), vec.end());
			}

			priority_queue<pair<size_t, uint64_t>> maxHeap;
			for (const auto& kv : pairCounts)
				if (kv.second >= config.minPairFrequency)
					maxHeap.push({ kv.second, kv.first });

			unordered_map<uint64_t, MergeRule> merges;
			size_t targetVocabSize = config.vocabSize > 0 ? config.vocabSize : (size_t)-1;

			while (vocab.size() < targetVocabSize && !maxHeap.empty())
			{
				auto [count, key] = maxHeap.top();
				maxHeap.pop();

				auto itCount = pairCounts.find(key);
				if (itCount == pairCounts.end() || itCount->second != count || count < config.minPairFrequency)
					continue;

				uint32_t id1 = (uint32_t)(key >> 32);
				uint32_t id2 = (uint32_t)(key & 0xFFFFFFFF);

				if (vocab[id1].size() + vocab[id2].size() > config.maxTokenLength)
				{
					pairCounts[key] = 0;
					continue;
				}

				uint32_t newId = (uint32_t)vocab.size();
				string newStr = vocab[id1] + vocab[id2];
				vocab.push_back(newStr);
				vocabToId[newStr] = newId;
				merges[key] = { (uint32_t)merges.size(), newId };

				auto itWords = tokenToWords.find(id1);
				if (itWords == tokenToWords.end()) continue;

				// Copy before iterating: tokenToWords[newId].push_back() below can
				// trigger a rehash and invalidate itWords->second.
				const vector<size_t> candidateWords = itWords->second;
				for (size_t wIdx : candidateWords)
				{
					auto& w = words[wIdx];
					if (w.tokens.size() < 2) continue;

					bool hasPair = false;
					for (size_t i = 0; i + 1 < w.tokens.size(); ++i)
						if (w.tokens[i] == id1 && w.tokens[i + 1] == id2) { hasPair = true; break; }
					if (!hasPair) continue;

					for (size_t i = 0; i + 1 < w.tokens.size(); ++i)
					{
						const uint64_t pKey = makeKey(w.tokens[i], w.tokens[i + 1]);
						auto& pc = pairCounts[pKey];
						pc -= w.count;
						if (pc >= config.minPairFrequency)
							maxHeap.push({ pc, pKey });
					}

					vector<uint32_t> newTokens;
					newTokens.reserve(w.tokens.size());
					for (size_t i = 0; i < w.tokens.size(); )
					{
						if (i + 1 < w.tokens.size() && w.tokens[i] == id1 && w.tokens[i + 1] == id2)
						{ newTokens.push_back(newId); i += 2; }
						else
						{ newTokens.push_back(w.tokens[i]); ++i; }
					}
					w.tokens = move(newTokens);

					for (size_t j = 0; j + 1 < w.tokens.size(); ++j)
					{
						const uint64_t pKey = makeKey(w.tokens[j], w.tokens[j + 1]);
						auto& pc = pairCounts[pKey];
						pc += w.count;
						if (pc >= config.minPairFrequency)
							maxHeap.push({ pc, pKey });
					}

					tokenToWords[newId].push_back(wIdx);
				}
			}

			return BpeTokenizer(move(vocab), move(merges), config.addPrefixSpace);
		}
	};

	// =========================================================================
	// BpeTokenizer  (unchanged)
	// =========================================================================

	static vector<uint32_t> buildByteToCharPos(const string& str)
	{
		vector<uint32_t> byteToCharPos;
		byteToCharPos.reserve(str.size() + 1);

		uint32_t chrPos = 0;
		size_t i = 0;
		size_t n = str.size();

		while (i < n)
		{
			unsigned char b = (unsigned char)str[i];
			size_t len = 1;
			bool isSurrogatePair = false;

			if ((b & 0x80) == 0)
			{
				len = 1;
			}
			else if ((b & 0xE0) == 0xC0)
			{
				len = 2;
			}
			else if ((b & 0xF0) == 0xE0)
			{
				len = 3;
			}
			else if ((b & 0xF8) == 0xF0)
			{
				len = 4;
				isSurrogatePair = true;
			}

			if (i + len > n) len = n - i;

			for (size_t k = 0; k < len; ++k)
			{
				byteToCharPos.push_back(chrPos);
			}

			chrPos += isSurrogatePair ? 2 : 1;
			i += len;
		}
		byteToCharPos.push_back(chrPos);
		return byteToCharPos;
	}

	bool BpeTokenizer::ready() const
	{
		return !vocab.empty();
	}

	void BpeTokenizer::encode(vector<uint32_t>& out, const string& str, vector<pair<uint32_t, uint32_t>>* offset, bool offsetInChrLevel) const
	{
		if (str.empty()) return;

		string workStr;
		bool prependedSpace = false;
		if (addPrefixSpace)
		{
			if (str[0] != ' ')
			{
				workStr = " " + str;
				prependedSpace = true;
			}
			else
			{
				workStr = str;
			}
		}
		else
		{
			workStr = str;
		}

		if (workStr.empty()) return;

		auto spans = extractChunkSpans(workStr);

		struct TokenSpan
		{
			uint32_t id;
			uint32_t start;
			uint32_t end;
		};

		auto makeKey = [](uint32_t a, uint32_t b) -> uint64_t {
			return ((uint64_t)a << 32) | (uint64_t)b;
		};

		vector<uint32_t> byteToCharPos;
		if (offset && offsetInChrLevel)
		{
			byteToCharPos = buildByteToCharPos(str);
		}

		for (const auto& span : spans)
		{
			vector<TokenSpan> tokens;
			tokens.reserve(span.second);
			for (size_t i = 0; i < span.second; ++i)
			{
				size_t origIdx = span.first + i;
				tokens.push_back({ (uint32_t)(unsigned char)workStr[origIdx], (uint32_t)origIdx, (uint32_t)(origIdx + 1) });
			}

			while (tokens.size() >= 2)
			{
				uint32_t bestRank = UINT32_MAX;
				uint32_t bestNewId = 0;

				for (size_t i = 0; i + 1 < tokens.size(); ++i)
				{
					uint64_t key = makeKey(tokens[i].id, tokens[i + 1].id);
					auto it = merges.find(key);
					if (it != merges.end() && it->second.rank < bestRank)
					{
						bestRank = it->second.rank;
						bestNewId = it->second.newId;
					}
				}

				if (bestRank == UINT32_MAX) break;

				vector<TokenSpan> newTokens;
				newTokens.reserve(tokens.size());

				size_t i = 0;
				while (i < tokens.size())
				{
					if (i + 1 < tokens.size())
					{
						uint64_t key = makeKey(tokens[i].id, tokens[i + 1].id);
						auto it = merges.find(key);
						if (it != merges.end() && it->second.rank == bestRank)
						{
							newTokens.push_back({ bestNewId, tokens[i].start, tokens[i + 1].end });
							i += 2;
							continue;
						}
					}
					newTokens.push_back(tokens[i]);
					i++;
				}
				tokens = move(newTokens);
			}

			for (const auto& tok : tokens)
			{
				out.push_back(tok.id);
				if (offset)
				{
					uint32_t sStart = prependedSpace ? (tok.start > 0 ? tok.start - 1 : 0) : tok.start;
					uint32_t sEnd   = prependedSpace ? (tok.end   > 0 ? tok.end   - 1 : 0) : tok.end;

					if (sStart > str.size()) sStart = (uint32_t)str.size();
					if (sEnd   > str.size()) sEnd   = (uint32_t)str.size();

					if (offsetInChrLevel)
					{
						uint32_t cStart = byteToCharPos[sStart];
						uint32_t cEnd   = byteToCharPos[sEnd];
						offset->emplace_back(cStart, cEnd);
					}
					else
					{
						offset->emplace_back(sStart, sEnd);
					}
				}
			}
		}
	}

	vector<uint32_t> BpeTokenizer::encode(const string& str, vector<pair<uint32_t, uint32_t>>* offset, bool offsetInChrLevel) const
	{
		vector<uint32_t> ret;
		encode(ret, str, offset, offsetInChrLevel);
		return ret;
	}

	template<class It>
	string BpeTokenizer::decode(It first, It last, bool ignoreErrors) const
	{
		string ret;
		for (; first != last; ++first)
		{
			uint32_t id = *first;
			if (id < vocab.size())
			{
				ret.append(vocab[id]);
			}
			else if (!ignoreErrors)
			{
				throw std::out_of_range("Token ID out of range in BpeTokenizer::decode: " + std::to_string(id));
			}
		}
		return ret;
	}

	string BpeTokenizer::decode(const vector<uint32_t>& ids, bool ignoreErrors) const
	{
		return decode(ids.begin(), ids.end(), ignoreErrors);
	}

	string BpeTokenizer::decode(const uint32_t* ids, size_t length, bool ignoreErrors) const
	{
		return decode(ids, ids + length, ignoreErrors);
	}

	// GPT-2 style bytes-to-unicode mapping.
	// Bytes that are printable (ASCII 33-126, Latin-1 supplement 161-172, 174-255)
	// map to themselves; the remaining 68 bytes map to U+0100..U+0143.
	static const array<char32_t, 256>& getBytesToUnicode()
	{
		static array<char32_t, 256> table = []() {
			array<char32_t, 256> t{};
			set<int> initial;
			for (int i = '!'; i <= '~'; ++i) initial.insert(i);
			for (int i = 0xA1; i <= 0xAC; ++i) initial.insert(i);
			for (int i = 0xAE; i <= 0xFF; ++i) initial.insert(i);
			for (int b : initial) t[b] = (char32_t)b;
			int n = 0;
			for (int b = 0; b < 256; ++b)
			{
				if (initial.find(b) == initial.end())
					t[b] = (char32_t)(256 + n++);
			}
			return t;
		}();
		return table;
	}

	static const unordered_map<char32_t, uint8_t>& getUnicodeToByte()
	{
		static unordered_map<char32_t, uint8_t> table = []() {
			unordered_map<char32_t, uint8_t> t;
			const auto& b2u = getBytesToUnicode();
			for (int i = 0; i < 256; ++i)
				t[b2u[i]] = (uint8_t)i;
			return t;
		}();
		return table;
	}

	// Encode a raw-byte string to the HF unicode representation (UTF-8 encoded)
	static string rawToHF(const string& s)
	{
		const auto& b2u = getBytesToUnicode();
		string ret;
		for (unsigned char c : s)
			utf8FromCode(ret, b2u[c]);
		return ret;
	}

	// Decode a HF unicode representation (UTF-8 encoded) back to raw bytes
	static string hfToRaw(const string& s)
	{
		const auto& u2b = getUnicodeToByte();
		string ret;
		for (size_t i = 0; i < s.size(); )
		{
			unsigned char b = (unsigned char)s[i];
			uint32_t code = 0;
			size_t len = 1;
			if      ((b & 0x80) == 0x00) { code = b;        len = 1; }
			else if ((b & 0xE0) == 0xC0) { code = b & 0x1F; len = 2; }
			else if ((b & 0xF0) == 0xE0) { code = b & 0x0F; len = 3; }
			else if ((b & 0xF8) == 0xF0) { code = b & 0x07; len = 4; }
			for (size_t j = 1; j < len && i + j < s.size(); ++j)
				code = (code << 6) | ((unsigned char)s[i + j] & 0x3F);
			auto it = u2b.find((char32_t)code);
			if (it != u2b.end())
				ret.push_back((char)it->second);
			i += len;
		}
		return ret;
	}

	ostream& BpeTokenizer::save(ostream& ostr) const
	{
		using json = nlohmann::json;

		// vocab: raw-byte token → HF unicode string, then write as {hfStr: id}
		json vocabJson = json::object();
		for (size_t i = 0; i < vocab.size(); ++i)
			vocabJson[rawToHF(vocab[i])] = (uint32_t)i;

		// merges: sort by rank, emit "hfA hfB" strings
		vector<pair<uint32_t, uint64_t>> sortedMerges;
		sortedMerges.reserve(merges.size());
		for (const auto& kv : merges)
			sortedMerges.push_back({ kv.second.rank, kv.first });
		sort(sortedMerges.begin(), sortedMerges.end());

		json mergesJson = json::array();
		for (const auto& [rank, key] : sortedMerges)
		{
			uint32_t id1 = (uint32_t)(key >> 32);
			uint32_t id2 = (uint32_t)(key & 0xFFFFFFFF);
			mergesJson.push_back(rawToHF(vocab[id1]) + " " + rawToHF(vocab[id2]));
		}

		json j;
		j["version"] = "1.0";
		j["truncation"] = nullptr;
		j["padding"] = nullptr;
		j["added_tokens"] = json::array();
		j["normalizer"] = nullptr;
		j["pre_tokenizer"] = {
			{"type", "ByteLevel"},
			{"add_prefix_space", addPrefixSpace},
			{"trim_offsets", true},
			{"use_regex", true}
		};
		j["post_processor"] = nullptr;
		j["decoder"] = {
			{"type", "ByteLevel"},
			{"add_prefix_space", addPrefixSpace},
			{"trim_offsets", true},
			{"use_regex", true}
		};
		j["model"] = {
			{"type", "BPE"},
			{"dropout", nullptr},
			{"unk_token", nullptr},
			{"continuing_subword_prefix", nullptr},
			{"end_of_word_suffix", nullptr},
			{"fuse_unk", false},
			{"byte_fallback", false},
			{"vocab", vocabJson},
			{"merges", mergesJson}
		};

		ostr << j.dump(2);
		return ostr;
	}

	BpeTokenizer BpeTokenizer::load(istream& istr)
	{
		using json = nlohmann::json;

		auto j = json::parse(istr);

		// Read addPrefixSpace from pre_tokenizer
		bool addPrefixSpace = false;
		if (j.contains("pre_tokenizer") && !j["pre_tokenizer"].is_null())
		{
			auto& pt = j["pre_tokenizer"];
			if (pt.contains("add_prefix_space") && pt["add_prefix_space"].is_boolean())
				addPrefixSpace = pt["add_prefix_space"].get<bool>();
		}

		auto& model = j["model"];

		// Build vocab sorted by ID
		vector<pair<uint32_t, string>> vocabPairs;
		for (auto& [hfToken, idVal] : model["vocab"].items())
			vocabPairs.push_back({ idVal.get<uint32_t>(), hfToRaw(hfToken) });
		sort(vocabPairs.begin(), vocabPairs.end());
		if (vocabPairs.empty())
			throw invalid_argument("BpeTokenizer::load: model vocab must not be empty");
		for (size_t i = 0; i < vocabPairs.size(); ++i)
		{
			if (vocabPairs[i].first != i)
				throw invalid_argument("BpeTokenizer::load: model vocab IDs must be unique and contiguous from zero");
		}

		vector<string> vocab;
		vocab.resize(vocabPairs.size());
		for (auto& [id, tok] : vocabPairs)
			vocab[id] = tok;

		// Reverse map for merge lookup
		unordered_map<string, uint32_t> vocabToId;
		vocabToId.reserve(vocab.size());
		for (size_t i = 0; i < vocab.size(); ++i)
			vocabToId[vocab[i]] = (uint32_t)i;

		auto makeKey = [](uint32_t a, uint32_t b) -> uint64_t {
			return ((uint64_t)a << 32) | (uint64_t)b;
		};

		// Build merges: index in list = rank
		unordered_map<uint64_t, MergeRule> merges;
		uint32_t rank = 0;
		for (auto& mergeVal : model["merges"])
		{
			string m = mergeVal.get<string>();
			auto sp = m.find(' ');
			if (sp == string::npos) { ++rank; continue; }

			string rawA = hfToRaw(m.substr(0, sp));
			string rawB = hfToRaw(m.substr(sp + 1));

			auto itA  = vocabToId.find(rawA);
			auto itB  = vocabToId.find(rawB);
			auto itAB = vocabToId.find(rawA + rawB);
			if (itA != vocabToId.end() && itB != vocabToId.end() && itAB != vocabToId.end())
				merges[makeKey(itA->second, itB->second)] = { rank, itAB->second };
			++rank;
		}

		return BpeTokenizer(move(vocab), move(merges), addPrefixSpace);
	}

	// =========================================================================
	// BpeTokenizerTrainer
	// =========================================================================

	BpeTokenizerTrainer::BpeTokenizerTrainer(const BpeTrainerConfig& config)
		: config(config)
	{
		if (config.vocabSize && config.vocabSize < 256)
			throw invalid_argument("BpeTokenizerTrainer::vocabSize must be zero or at least 256");
		if (!config.maxTokenLength)
			throw invalid_argument("BpeTokenizerTrainer::maxTokenLength must be positive");
		if (!config.batchSize)
			throw invalid_argument("BpeTokenizerTrainer::batchSize must be positive");
		if (config.largeCounter)
			impl = make_unique<BpeTokenizerTrainerImpl<true>>();
		else
			impl = make_unique<BpeTokenizerTrainerImpl<false>>();
	}

	BpeTokenizerTrainer::~BpeTokenizerTrainer() = default;

	size_t BpeTokenizerTrainer::addSentences(const std::function<std::string()>& feeder)
	{
		return impl->addSentences(sentenceCount, config, feeder);
	}

	size_t BpeTokenizerTrainer::addSentences(const std::function<std::u16string()>& feeder)
	{
		return impl->addSentences(sentenceCount, config, [&]()
		{
			u16string u16 = feeder();
			return u16.empty() ? string{} : utf16To8(u16);
		});
	}

	BpeTokenizer BpeTokenizerTrainer::build() const
	{
		return impl->build(config);
	}
}
