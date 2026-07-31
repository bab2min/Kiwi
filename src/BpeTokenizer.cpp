#include <kiwi/BpeTokenizer.h>
#include <kiwi/ThreadPool.h>
#include <kiwi/Utils.h>
#include "StrUtils.h"
#include "UnicodeCase.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <exception>
#include <limits>
#include <queue>
#include <set>
#include <unordered_set>
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

		void appendVarint(size_t v)
		{
			while (v >= 0x80)
			{
				data.push_back(static_cast<char>(v | 0x80));
				v >>= 7;
			}
			data.push_back(static_cast<char>(v));
		}

		static const char* decodeVarint(const char* p, size_t& out)
		{
			size_t v = 0;
			int shift = 0;
			for (;;)
			{
				const auto b = static_cast<unsigned char>(*p++);
				v |= static_cast<size_t>(b & 0x7F) << shift;
				if (!(b & 0x80)) break;
				shift += 7;
			}
			out = v;
			return p;
		}

		static size_t varintSize(size_t v)
		{
			size_t n = 1;
			while (v >= 0x80) { v >>= 7; ++n; }
			return n;
		}

	public:
		// For largeCounter=false: stores a varint length prefix followed by the raw
		// bytes, and returns a uint32_t offset.  The explicit length keeps chunks that
		// contain NUL bytes intact (a NUL terminator would truncate them) and lets
		// key comparison skip strlen().  Arena size is limited to 4 GB.
		uint32_t intern(string_view sv)
		{
			assert(!sv.empty());
			if (data.size() + varintSize(sv.size()) + sv.size() > UINT32_MAX)
				throw length_error{ "BpeTokenizerTrainer: StringArena exceeded 4 GB; set BpeTrainerConfig::largeCounter to true" };
			const uint32_t offset = static_cast<uint32_t>(data.size());
			appendVarint(sv.size());
			data.insert(data.end(), sv.begin(), sv.end());
			return offset;
		}

		string_view view(uint32_t offset) const
		{
			size_t length = 0;
			const char* p = decodeVarint(data.data() + offset, length);
			return { p, length };
		}

		// For largeCounter=true: stores raw bytes, returns packed handle.
		//   bits [63:24] = byte offset (40 bits, up to ~1 TB)
		//   bits [23: 0] = length      (24 bits, up to 16 MB)
		uint64_t internPacked(string_view sv)
		{
			assert(!sv.empty());
			if (sv.size() > 0xFFFFFF)
				throw length_error{ "BpeTokenizerTrainer: chunk longer than 16 MB" };
			const uint64_t offset = static_cast<uint64_t>(data.size());
			if (offset > 0xFFFFFFFFFFull)
				throw length_error{ "BpeTokenizerTrainer: StringArena offset overflow (> 1 TB)" };
			data.insert(data.end(), sv.begin(), sv.end());
			return (offset << 24) | static_cast<uint64_t>(sv.size());
		}

		string_view viewPacked(uint64_t packed) const
		{
			return { data.data() + (packed >> 24), packed & 0xFFFFFF };
		}

		// Resets content but keeps allocated capacity for reuse across batches.
		void clear() { data.clear(); }
	};

	// -------------------------------------------------------------------------
	// WordCountMap<largeCounter>  –  open-addressing hash map that owns its arena.
	//
	// largeCounter=false: 8-byte slots  {uint32_t offset, uint32_t count}
	//   Key = length-prefixed string at arena[offset].  Sentinel: offset==UINT32_MAX.
	//   Arena size limit: 4 GB.  Count limit: ~4 billion.
	//
	// largeCounter=true: 16-byte slots  {uint64_t packed, uint64_t count}
	//   Key = packed handle (bits[63:24]=offset 40b, bits[23:0]=length 24b).
	//   Arena size limit: ~1 TB.  Count limit: ~1.8e19.
	//
	// Exceeding either limit throws rather than silently wrapping.
	//
	// The arena is held by value (not by pointer) so the implicitly generated copy
	// and move operations stay correct; an earlier layout kept a back-pointer into a
	// sibling member, which would dangle as soon as the object was copied or moved.
	//
	// Instances are used one-per-worker inside a vector, so the type is padded to a
	// cache line: the hot members (slots/used/arena sizes) are written on every add()
	// and would otherwise false-share between adjacent workers.
	// -------------------------------------------------------------------------
	template<bool largeCounter>
	class alignas(64) WordCountMap
	{
		using KeyT   = conditional_t<largeCounter, uint64_t, uint32_t>;
		using CountT = conditional_t<largeCounter, uint64_t, uint32_t>;
		struct Slot { KeyT key; CountT count; }; // 8B or 16B
		static constexpr KeyT emptyKey = largeCounter ? KeyT(0) : KeyT(UINT32_MAX);

		StringArena arena;
		vector<Slot> slots;
		size_t used = 0;

		static uint64_t fnv1a(string_view sv) noexcept
		{
			uint64_t h = 14695981039346656037ull;
			for (unsigned char c : sv) h = (h ^ c) * 1099511628211ull;
			return h;
		}

		string_view slotView(const Slot& s) const
		{
			if constexpr (largeCounter) return arena.viewPacked(s.key);
			else return arena.view(s.key);
		}

		void grow()
		{
			const size_t newCap = slots.empty() ? 64 : slots.size() * 2;
			vector<Slot> newSlots(newCap, Slot{ emptyKey, 0 });
			const size_t mask = newCap - 1;
			for (auto& s : slots)
			{
				if (s.key == emptyKey) continue;
				size_t idx = static_cast<size_t>(fnv1a(slotView(s))) & mask;
				while (newSlots[idx].key != emptyKey) idx = (idx + 1) & mask;
				newSlots[idx] = s;
			}
			slots = move(newSlots);
		}

		static void checkCounterRoom(CountT current, size_t delta)
		{
			const uint64_t room = static_cast<uint64_t>(numeric_limits<CountT>::max()) - current;
			if (static_cast<uint64_t>(delta) > room)
				throw overflow_error{ "BpeTokenizerTrainer: chunk count exceeded 32 bits; set BpeTrainerConfig::largeCounter to true" };
		}

	public:
		void add(string_view sv, size_t delta = 1)
		{
			if (used * 5 >= slots.size() * 3) grow(); // 60% load factor

			const size_t mask = slots.size() - 1;
			size_t idx = static_cast<size_t>(fnv1a(sv)) & mask;
			while (slots[idx].key != emptyKey)
			{
				if (slotView(slots[idx]) == sv)
				{
					checkCounterRoom(slots[idx].count, delta);
					slots[idx].count += static_cast<CountT>(delta);
					return;
				}
				idx = (idx + 1) & mask;
			}
			checkCounterRoom(0, delta);
			if constexpr (largeCounter)
				slots[idx] = { arena.internPacked(sv), static_cast<CountT>(delta) };
			else
				slots[idx] = { arena.intern(sv), static_cast<CountT>(delta) };
			++used;
		}

		// Resets content but keeps allocated capacity for reuse across batches.
		void clear()
		{
			arena.clear();
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

	// BpeTokenizerTrainer::Impl  –  virtual interface.
	// Concrete type is BpeTokenizerTrainerImpl<largeCounter> defined below,
	// after the UTF-8 / chunk-extraction helpers it depends on.
	struct BpeTokenizerTrainer::Impl
	{
		virtual ~Impl() = default;
		virtual size_t addSentences(size_t& sentenceCount, const BpeTrainerConfig& config,
		                            const BpeTokenizerTrainerEventCallback& callback,
		                            const function<string()>& feeder) = 0;
		virtual BpeTokenizer build(const BpeTrainerConfig& config,
		                           const BpeTokenizerTrainerEventCallback& callback) const = 0;
	};

	// Progress reporting is opt-in; skip the std::function indirection entirely when
	// no callback was installed.
	static inline void emitEvent(const BpeTokenizerTrainerEventCallback& callback,
	                             BpeTokenizerTrainerEvent event, size_t current, size_t total)
	{
		if (callback) callback(event, current, total);
	}

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

	enum class ChrClass : uint8_t { letter, number, space, other };

	static ChrClass classifyCodepoint(char32_t c)
	{
		if (isUnicodeLetter(c)) return ChrClass::letter;
		if (isUnicodeNumber(c)) return ChrClass::number;
		if (isUnicodeSpace(c)) return ChrClass::space;
		return ChrClass::other;
	}

	// Writes the chunk spans of `str` into `chunks`.  The caller owns the buffer so
	// that hot loops can reuse a single allocation across sentences.  Each byte
	// position is UTF-8 decoded exactly once.
	static void extractChunkSpans(const string& str, vector<pair<size_t, size_t>>& chunks)
	{
		chunks.clear();
		if (str.empty()) return;

		const size_t n = str.size();
		size_t i = 0;

		while (i < n)
		{
			const size_t start = i;

			// 1. Contractions
			if (str[i] == '\'' && i + 1 < n)
			{
				const char c1 = (char)tolower((unsigned char)str[i + 1]);
				const char c2 = (i + 2 < n) ? (char)tolower((unsigned char)str[i + 2]) : '\0';

				if (c1 == 's' || c1 == 't' || c1 == 'm' || c1 == 'd')
				{
					i += 2;
					chunks.push_back({ start, i - start });
					continue;
				}
				else if ((c1 == 'r' && c2 == 'e') || (c1 == 'v' && c2 == 'e') || (c1 == 'l' && c2 == 'l'))
				{
					i += 3;
					chunks.push_back({ start, i - start });
					continue;
				}
			}

			// 2. Spaces
			if (classifyCodepoint(decodeUtf8Codepoint(str, i).value) == ChrClass::space)
			{
				size_t j = i;
				size_t lastSpace = i;
				while (j < n)
				{
					const auto cp = decodeUtf8Codepoint(str, j);
					if (classifyCodepoint(cp.value) != ChrClass::space) break;
					lastSpace = j;
					j += cp.size;
				}

				if (j == n)
				{
					chunks.push_back({ start, j - start });
					i = j;
					continue;
				}

				if (str[lastSpace] == ' ')
				{
					if (j - i > 1)
					{
						chunks.push_back({ start, (j - 1) - start });
						i = j - 1;
						continue;
					}
				}
				else
				{
					chunks.push_back({ start, j - start });
					i = j;
					continue;
				}
			}

			// 3. Optional space followed by a run of Letter, Number, or Other
			if (i < n && str[i] == ' ')
			{
				i++;
			}

			if (i < n)
			{
				const auto cp = decodeUtf8Codepoint(str, i);
				const ChrClass cls = classifyCodepoint(cp.value);
				if (cls != ChrClass::space)
				{
					i += cp.size;
					while (i < n)
					{
						const auto next = decodeUtf8Codepoint(str, i);
						if (classifyCodepoint(next.value) != cls) break;
						i += next.size;
					}
				}
			}

			chunks.push_back({ start, i - start });
		}
	}

	static vector<pair<size_t, size_t>> extractChunkSpans(const string& str)
	{
		vector<pair<size_t, size_t>> chunks;
		extractChunkSpans(str, chunks);
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
		WordCountMap<largeCounter> wordCounts;

		// ---- chunk extraction --------------------------------------------------

		// `prefixBuf` and `spanBuf` are caller-owned scratch buffers, reused across
		// sentences so that the per-sentence path performs no heap allocation.
		static void addChunksTo(const string& str, bool addPrefixSpace,
		                        WordCountMap<largeCounter>& wc,
		                        string& prefixBuf, vector<pair<size_t, size_t>>& spanBuf)
		{
			const string* workStr = &str;
			if (addPrefixSpace && !str.empty() && str[0] != ' ')
			{
				prefixBuf.assign(1, ' ');
				prefixBuf += str;
				workStr = &prefixBuf;
			}
			extractChunkSpans(*workStr, spanBuf);
			for (auto& span : spanBuf)
				wc.add(string_view(workStr->data() + span.first, span.second));
		}

		// ---- addSentences ------------------------------------------------------

		size_t addSentences(size_t& sentenceCount, const BpeTrainerConfig& config,
		                    const BpeTokenizerTrainerEventCallback& callback,
		                    const function<string()>& feeder) override
		{
			emitEvent(callback, BpeTokenizerTrainerEvent::pretokenizeBegin, 0, 0);

			size_t totalCount = 0;
			vector<string> batch;
			batch.reserve(config.batchSize);
			const size_t maxWorkerCount = getWorkerCount(config.numThreads);
			unique_ptr<utils::ThreadPool> pool;
			if (maxWorkerCount > 1)
				pool = make_unique<utils::ThreadPool>(maxWorkerCount);

			// Per-worker accumulators: allocated once, reused across batches.
			vector<WordCountMap<largeCounter>> localCounts(maxWorkerCount);

			// `feeder` signals end of input by returning an empty string.  Track that
			// explicitly so it is never called again afterwards: the previous shape of
			// this loop re-entered and called `feeder` once more past the sentinel,
			// which silently required every feeder to be sticky at EOF.
			bool eof = false;
			while (!eof)
			{
				batch.clear();
				for (size_t i = 0; i < config.batchSize; ++i)
				{
					string sentence = feeder();
					if (sentence.empty())
					{
						eof = true;
						break;
					}
					batch.emplace_back(move(sentence));
				}
				if (batch.empty()) break;

				const size_t workerCount = min(maxWorkerCount, batch.size());
				// Clear every accumulator, not just the ones about to be used: a batch
				// smaller than the previous one would otherwise leave the tail workers
				// holding their whole retained table until addSentences returns.
				for (auto& lc : localCounts) lc.clear();

				auto processRange = [&](size_t workerIndex)
				{
					const size_t begin = batch.size() * workerIndex / workerCount;
					const size_t end   = batch.size() * (workerIndex + 1) / workerCount;
					string prefixBuf;
					vector<pair<size_t, size_t>> spanBuf;
					for (size_t i = begin; i < end; ++i)
						addChunksTo(batch[i], config.addPrefixSpace, localCounts[workerIndex], prefixBuf, spanBuf);
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

					// Wait for every worker before rethrowing.  Bailing out on the first
					// exception would destroy the remaining futures while their tasks are
					// still running against this frame's `batch` and `localCounts`.
					exception_ptr firstError;
					for (auto& f : futures)
					{
						try { f.get(); }
						catch (...) { if (!firstError) firstError = current_exception(); }
					}
					if (firstError) rethrow_exception(firstError);
				}

				for (size_t i = 0; i < workerCount; ++i)
					localCounts[i].forEach([&](string_view sv, size_t count)
					{
						wordCounts.add(sv, count);
					});

				totalCount    += batch.size();
				sentenceCount += batch.size();

				// One event per batch: the feeder gives no length, so the count is all
				// that can be reported and the batch is the natural granularity.
				emitEvent(callback, BpeTokenizerTrainerEvent::pretokenizeProgress, totalCount, 0);
			}

			emitEvent(callback, BpeTokenizerTrainerEvent::pretokenizeEnd, totalCount, totalCount);
			return totalCount;
		}

		// ---- build -------------------------------------------------------------

		BpeTokenizer build(const BpeTrainerConfig& config,
		                   const BpeTokenizerTrainerEventCallback& callback) const override
		{
			const size_t targetVocabSize = config.vocabSize > 0 ? config.vocabSize : (size_t)-1;
			// Zero when unbounded: the callback contract uses it for "indeterminate".
			const size_t reportedTotal = config.vocabSize;

			vector<string> vocab;
			// vocabSize is validated to be zero or >= 256; cap the up-front reservation
			// so that an absurdly large request does not allocate before training.
			vocab.reserve(min(config.vocabSize ? config.vocabSize : (size_t)256, (size_t)1 << 20));
			unordered_map<string, uint32_t> vocabToId;

			for (int i = 0; i < 256; ++i)
			{
				string s(1, (char)(unsigned char)i);
				vocabToId.emplace(s, (uint32_t)i);
				vocab.push_back(move(s));
			}

			// `mask` folds the word's token ids into 64 buckets, one bit each.  A clear
			// bit proves the token is absent; a set bit only suggests it may be present.
			// That one-sided guarantee is enough to reject a candidate word without
			// touching its token array, which is where most of the merge loop went: 84%
			// of candidate visits scanned a word that did not contain id2 at all.
			auto tokenBit = [](uint32_t t) -> uint64_t { return 1ull << (t & 63); };

			struct Word { vector<uint32_t> tokens; size_t count; uint64_t mask; };

			vector<Word> words;
			words.reserve(wordCounts.size());
			wordCounts.forEach([&](string_view sv, size_t count)
			{
				Word w;
				w.count = count;
				w.mask = 0;
				w.tokens.reserve(sv.size());
				for (unsigned char c : sv)
				{
					w.tokens.push_back((uint32_t)c);
					w.mask |= tokenBit((uint32_t)c);
				}
				words.push_back(move(w));
			});
			if (words.size() > numeric_limits<uint32_t>::max())
				throw length_error{ "BpeTokenizerTrainer: more than 2^32 distinct chunks" };

			auto makeKey = [](uint32_t a, uint32_t b) -> uint64_t {
				return ((uint64_t)a << 32) | (uint64_t)b;
			};

			// `pushed` is the value of this pair's single authoritative heap entry, and
			// is always >= `count`.  Tracking it turns the heap into a lazy decrease-key
			// structure: a pair is pushed only when its count rises above the entry it
			// already has, and any popped entry that does not match `pushed` is a stale
			// duplicate that can be dropped outright.  Without it, rewriting a word
			// pushes every one of its pairs even though only the two adjacent to the
			// merge site actually changed.
			struct PairStat { size_t count = 0; size_t pushed = 0; };
			unordered_map<uint64_t, PairStat> pairCounts;
			for (const auto& w : words)
				for (size_t i = 0; i + 1 < w.tokens.size(); ++i)
					pairCounts[makeKey(w.tokens[i], w.tokens[i + 1])].count += w.count;

			// tokenToWords[t] = indices of the words that contain token t, ascending
			// and deduplicated.  Indexed directly by token id (ids are dense), and
			// filled in two passes so that each of the 256 byte-token lists is
			// allocated at exactly its final size: pushing one entry per occurrence
			// and deduplicating afterwards peaked at roughly 4x this footprint.
			vector<vector<uint32_t>> tokenToWords(vocab.size());
			{
				array<bool, 256> seen{};
				array<uint32_t, 256> distinctCount{};
				for (const auto& w : words)
				{
					for (uint32_t t : w.tokens) if (!seen[t]) { seen[t] = true; ++distinctCount[t]; }
					for (uint32_t t : w.tokens) seen[t] = false;
				}
				for (size_t t = 0; t < 256; ++t) tokenToWords[t].reserve(distinctCount[t]);
				for (size_t wIdx = 0; wIdx < words.size(); ++wIdx)
				{
					const auto& w = words[wIdx];
					for (uint32_t t : w.tokens)
						if (!seen[t]) { seen[t] = true; tokenToWords[t].push_back((uint32_t)wIdx); }
					for (uint32_t t : w.tokens) seen[t] = false;
				}
			}

			priority_queue<pair<size_t, uint64_t>> maxHeap;
			for (auto& kv : pairCounts)
			{
				if (kv.second.count >= config.minPairFrequency)
				{
					kv.second.pushed = kv.second.count;
					maxHeap.push({ kv.second.count, kv.first });
				}
			}

			unordered_map<uint64_t, MergeRule> merges;
			// Pairs rejected by maxTokenLength.  Kept in a separate set rather than
			// zeroing pairCounts: the pair is still adjacent inside words, so a zeroed
			// counter would wrap around on the next decrement and flood the heap with
			// astronomically large phantom counts.
			unordered_set<uint64_t> blockedPairs;

			emitEvent(callback, BpeTokenizerTrainerEvent::mergeBegin, vocab.size(), reportedTotal);

			while (vocab.size() < targetVocabSize && !maxHeap.empty())
			{
				const auto top = maxHeap.top();
				maxHeap.pop();
				const size_t count = top.first;
				const uint64_t key = top.second;

				if (blockedPairs.count(key)) continue;

				const auto itCount = pairCounts.find(key);
				if (itCount == pairCounts.end()) continue;   // pair no longer exists
				PairStat& stat = itCount->second;
				// Not this pair's authoritative entry: a superseded duplicate, so drop it
				// without a replacement push.
				if (count != stat.pushed) continue;
				if (stat.count < config.minPairFrequency)
				{
					// Retire the entry, and reset `pushed` so that a later rise past the
					// threshold is guaranteed to push a fresh one.
					stat.pushed = 0;
					continue;
				}
				if (stat.count != count)
				{
					// Over-stated snapshot.  Correct it and re-insert rather than
					// discarding: discarding would lose the pair outright, which is why
					// every decrement below used to push a replacement entry.  Every
					// push carries a count >= the pair's true count, so the first entry
					// that matches its true count is the true maximum.
					stat.pushed = stat.count;
					maxHeap.push({ stat.count, key });
					continue;
				}

				const uint32_t id1 = (uint32_t)(key >> 32);
				const uint32_t id2 = (uint32_t)(key & 0xFFFFFFFF);

				if (vocab[id1].size() + vocab[id2].size() > config.maxTokenLength)
				{
					blockedPairs.insert(key);
					// Saturate `pushed` so the pair is never pushed again: it stays
					// adjacent inside words, so its count keeps moving.
					stat.pushed = numeric_limits<size_t>::max();
					continue;
				}

				string newStr = vocab[id1] + vocab[id2];
				uint32_t newId;
				const auto itVocab = vocabToId.find(newStr);
				if (itVocab != vocabToId.end())
				{
					// Two distinct pairs can produce the same string.  Reuse the existing
					// id instead of appending a duplicate entry: save() keys the vocab
					// JSON by token string, so duplicates would collapse on write and
					// make the resulting file unloadable.
					newId = itVocab->second;
				}
				else
				{
					newId = (uint32_t)vocab.size();
					vocabToId.emplace(newStr, newId);
					vocab.push_back(move(newStr));
					tokenToWords.resize(vocab.size());
					emitEvent(callback, BpeTokenizerTrainerEvent::mergeProgress, vocab.size(), reportedTotal);
				}
				merges.emplace(key, MergeRule{ (uint32_t)merges.size(), newId });

				// newId differs from id1 and id2 (its string is strictly longer than
				// either), so `candidates` is not appended to inside this loop and the
				// outer vector is not resized either; the reference stays valid.
				//
				// The list is also compacted in place while it is walked: a word that no
				// longer contains id1 can never match again, yet nothing used to remove
				// it, so the lists only ever grew.  On a 5 MB corpus that left 97.6% of
				// the 58M candidate visits scanning words that could not match.  The
				// write cursor trails the read cursor, so this is allocation-free.
				const uint64_t bit1 = tokenBit(id1), needMask = bit1 | tokenBit(id2);

				auto& candidates = tokenToWords[id1];
				const size_t candidateCount = candidates.size();
				size_t keptCount = 0;
				for (size_t k = 0; k < candidateCount; ++k)
				{
					const uint32_t wIdx = candidates[k];
					auto& w = words[wIdx];

					if ((w.mask & needMask) != needMask)
					{
						// At least one of the two tokens is provably absent, so the pair
						// cannot occur.  The entry is still kept whenever id1's bit is
						// set, because a set bit does not prove id1 is really there —
						// dropping on it would break the superset invariant.
						if (w.mask & bit1) candidates[keptCount++] = wIdx;
						continue;
					}

					// One scan answers both questions: does the pair occur here, and is
					// id1 still present at all (i.e. is this entry worth keeping)?
					bool hasPair = false, hasId1 = false;
					for (size_t i = 0; i < w.tokens.size(); ++i)
					{
						if (w.tokens[i] != id1) continue;
						hasId1 = true;
						if (i + 1 < w.tokens.size() && w.tokens[i + 1] == id2) { hasPair = true; break; }
					}
					if (!hasPair)
					{
						if (hasId1) candidates[keptCount++] = wIdx;
						continue;
					}

					// Withdraw every pair of the current token sequence...
					for (size_t i = 0; i + 1 < w.tokens.size(); ++i)
					{
						const uint64_t pKey = makeKey(w.tokens[i], w.tokens[i + 1]);
						const auto it = pairCounts.find(pKey);
						assert(it != pairCounts.end());
						if (it == pairCounts.end()) continue;
						// Counts are unsigned; drop the entry instead of ever wrapping.
						if (it->second.count <= w.count) { pairCounts.erase(it); continue; }
						// No push: a decrement can never make this pair the new maximum,
						// and its existing (now over-stated) entry is corrected when it
						// reaches the top of the heap.
						it->second.count -= w.count;
					}

					// ...rewrite the word in place (the result is never longer, so the
					// write cursor always trails the read cursor), rebuilding the mask
					// from scratch as the tokens are written out: the merge introduces
					// newId and may retire id1 or id2, and an OR-only update could never
					// clear a bit...
					size_t out = 0;
					uint64_t newMask = 0;
					for (size_t i = 0; i < w.tokens.size(); )
					{
						uint32_t t;
						if (i + 1 < w.tokens.size() && w.tokens[i] == id1 && w.tokens[i + 1] == id2)
						{ t = newId; i += 2; }
						else
						{ t = w.tokens[i]; ++i; }
						w.tokens[out++] = t;
						newMask |= tokenBit(t);
					}
					w.tokens.resize(out);
					w.mask = newMask;

					// ...and re-deposit every pair of the new sequence.
					for (size_t j = 0; j + 1 < w.tokens.size(); ++j)
					{
						const uint64_t pKey = makeKey(w.tokens[j], w.tokens[j + 1]);
						PairStat& ps = pairCounts[pKey];
						ps.count += w.count;
						// Only push when the count outgrows the entry this pair already
						// has.  A pair away from the merge site is withdrawn and then
						// re-deposited by the same amount, so it lands back at or below
						// `pushed` and needs no entry at all — that case alone accounted
						// for most of the heap traffic.
						if (ps.count >= config.minPairFrequency && ps.count > ps.pushed)
						{
							ps.pushed = ps.count;
							maxHeap.push({ ps.count, pKey });
						}
					}

					// Keep the entry only while id1 actually survives in the rewritten
					// word; every occurrence is usually consumed by the merge, and the
					// mask settles that common case without a scan.
					if ((w.mask & bit1) && find(w.tokens.begin(), w.tokens.end(), id1) != w.tokens.end())
						candidates[keptCount++] = wIdx;

					tokenToWords[newId].push_back(wIdx);
				}
				candidates.resize(keptCount);

				// Every occurrence reachable through the index has been withdrawn, so the
				// pair is normally erased by now.  Should any residue remain, re-arm it:
				// decrements no longer push, so it would otherwise never be revisited.
				// Requiring a strict decrease keeps this loop finite.
				const auto itResidue = pairCounts.find(key);
				if (itResidue != pairCounts.end())
				{
					PairStat& rs = itResidue->second;
					if (rs.count < count && rs.count >= config.minPairFrequency && rs.pushed != rs.count)
					{
						rs.pushed = rs.count;
						maxHeap.push({ rs.count, key });
					}
				}
			}

			emitEvent(callback, BpeTokenizerTrainerEvent::mergeEnd, vocab.size(), vocab.size());

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

	BpeTokenizerTrainer::BpeTokenizerTrainer(const BpeTrainerConfig& config, BpeTokenizerTrainerEventCallback callback)
		: config(config), callback(move(callback))
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
		return impl->addSentences(sentenceCount, config, callback, feeder);
	}

	size_t BpeTokenizerTrainer::addSentences(const std::function<std::u16string()>& feeder)
	{
		return impl->addSentences(sentenceCount, config, callback, [&]()
		{
			u16string u16 = feeder();
			return u16.empty() ? string{} : utf16To8(u16);
		});
	}

	BpeTokenizer BpeTokenizerTrainer::build() const
	{
		return impl->build(config, callback);
	}
}
