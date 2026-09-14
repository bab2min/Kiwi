#include <kiwi/BpeTokenizer.h>
#include <kiwi/Kiwi.h>
#include <kiwi/TagUtils.h>
#include <kiwi/ThreadPool.h>
#include <kiwi/Utils.h>
#include "StrUtils.h"
#include "UnicodeCase.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <queue>
#include <set>
#include <unordered_set>
#include <cctype>
#include <ostream>
#include <istream>
#include <nlohmann/json.hpp>

using namespace std;

namespace kiwi
{
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
		// largeCounter=false: varint length prefix + raw bytes (keys may contain NUL); returns a uint32_t offset (arena < 4 GB).
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

		// largeCounter=true: raw bytes; returns (offset << 24) | length, i.e. a 40-bit offset and a 24-bit length.
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
			return { data.data() + (packed >> 24), (size_t)(packed & 0xFFFFFF) };
		}

		void clear() { data.clear(); }
	};

	// Open-addressing word -> count map that owns its key arena. Exceeding the arena or count limit throws.
	template<bool largeCounter>
	class alignas(64) WordCountMap
	{
		using KeyT   = conditional_t<largeCounter, uint64_t, uint32_t>;
		using CountT = conditional_t<largeCounter, uint64_t, uint32_t>;
		struct Slot { KeyT key; CountT count; };
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
			const uint64_t room = (uint64_t)((CountT)-1) - current;
			if ((uint64_t)delta > room)
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
			{
				slots[idx] = { arena.internPacked(sv), static_cast<CountT>(delta) };
			}
			else
			{
				slots[idx] = { arena.intern(sv), static_cast<CountT>(delta) };
			}
			++used;
		}

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
			{
				if (s.key != emptyKey) f(slotView(s), static_cast<size_t>(s.count));
			}
		}
	};

	struct BpeTokenizerTrainer::Impl
	{
		virtual ~Impl() = default;
		virtual size_t addSentences(size_t& sentenceCount, const BpeTrainerConfig& config,
									const Kiwi* kiwi,
		                            const BpeTokenizerTrainerEventCallback& callback,
		                            const function<string()>& feeder) = 0;
		virtual BpeTokenizer build(const BpeTrainerConfig& config,
		                           const BpeTokenizerTrainerEventCallback& callback) const = 0;
	};

	static inline void emitEvent(const BpeTokenizerTrainerEventCallback& callback,
	                             BpeTokenizerTrainerEvent event, size_t current, size_t total)
	{
		if (callback) callback(event, current, total);
	}

	struct Utf8Codepoint
	{
		char32_t value;
		size_t size;
	};

	// Invalid UTF-8 bytes are kept as individual non-letter/non-number bytes so
	// byte-level tokenization remains lossless for arbitrary std::string input.
	static Utf8Codepoint decodeUtf8Codepoint(string_view str, size_t pos)
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

	static constexpr char32_t hangulSyllableFirst = 0xAC00;
	static constexpr char32_t hangulSyllableLast  = 0xD7A3;
	static constexpr char32_t hangulOnsetFirst  = 0x1100;  // U+1100..U+1112
	static constexpr char32_t hangulVowelFirst = 0x1161;  // U+1161..U+1175
	static constexpr char32_t hangulCodaFirst  = 0x11A8;  // U+11A8..U+11C2
	static constexpr size_t hangulOnsetCount  = 19;
	static constexpr size_t hangulVowelCount = 21;
	static constexpr size_t hangulCodaCount  = 27;
	static constexpr size_t hangulCodaStride = hangulCodaCount + 1;

	static constexpr size_t hangulOnsetNucleusBytes = 6;

	static void decomposeHangul(string_view str, string& out, vector<uint32_t>* offsets = nullptr)
	{
		out.clear();
		out.reserve(str.size() * 2);
		if (offsets)
		{
			offsets->clear();
			offsets->reserve(str.size() + 1);
		}
		size_t i = 0;
		while (i < str.size())
		{
			const auto cp = decodeUtf8Codepoint(str, i);
			if (offsets) offsets->insert(offsets->end(), cp.size, (uint32_t)out.size());
			if (hangulSyllableFirst <= cp.value && cp.value <= hangulSyllableLast)
			{
				const size_t index = (size_t)(cp.value - hangulSyllableFirst);
				const size_t tail = index % hangulCodaStride;
				utf8FromCode(out, hangulOnsetFirst + (char32_t)(index / (hangulVowelCount * hangulCodaStride)));
				utf8FromCode(out, hangulVowelFirst + (char32_t)(index / hangulCodaStride % hangulVowelCount));
				if (tail) utf8FromCode(out, hangulCodaFirst + (char32_t)(tail - 1));
			}
			else
			{
				out.append(str.data() + i, cp.size);
			}
			i += cp.size;
		}
		if (offsets) offsets->push_back((uint32_t)out.size());
	}

	enum class ChrClass : uint8_t { letter, number, space, other };

	static ChrClass classifyCodepoint(char32_t c)
	{
		if (isUnicodeLetter(c)) return ChrClass::letter;
		if (isUnicodeNumber(c)) return ChrClass::number;
		if (isUnicodeSpace(c)) return ChrClass::space;
		return ChrClass::other;
	}

	inline bool isPretokenizingBoundary(PretokenizeOption option, POSTag prev, POSTag cur, char16_t curStr)
	{
		if (curStr == u'요' && cur == POSTag::jx)
		{
			if (isEClass(prev)) return false;
		}

		if (!!(option & PretokenizeOption::jClass))
		{
			if (isJClass(cur) != isJClass(prev))
			{
				return true;
			}
		}

		if (!!(option & PretokenizeOption::eClass))
		{
			if ((isEClass(cur) && !(isEClass(prev) 
				|| (!!(option & PretokenizeOption::vcp) && prev == POSTag::vcp) 
				|| (!!(option & PretokenizeOption::xsv) && (clearIrregular(prev) == POSTag::xsv || clearIrregular(prev) == POSTag::xsa))))
				|| (isEClass(prev) && !(isEClass(cur))))
			{
				return true;
			}
		}

		if (!!(option & PretokenizeOption::vcp))
		{
			if (cur == POSTag::vcp)
			{
				return true;
			}
		}

		if (!!(option & PretokenizeOption::xsv))
		{
			if (clearIrregular(cur) == POSTag::xsv || clearIrregular(cur) == POSTag::xsa)
			{
				return true;
			}
		}
		return false;
	}

	inline char16_t extractHangulCoda(char16_t c)
	{
		if (!(0xAC00 <= c && c < 0xD7A4)) return 0;
		const auto coda = (c - 0xAC00) % 28;
		return coda ? (char16_t)(0x11A7 + coda) : 0;
	}

	// Boundaries are byte positions of `str`, or of its decomposition (mapped via `jamoOffsets`) when useJamoAlphabet is on.
	static void collectMorphemeBoundaries(
		vector<size_t>& boundariesOut,
		const string& str,
		PretokenizeOption pretokenizeOption,
		bool useJamoAlphabet,
		const vector<uint32_t>& jamoOffsets,
		const Kiwi& kiwi
	)
	{
		boundariesOut.clear();

		auto mapOffset = [&](size_t p) -> size_t
		{
			return useJamoAlphabet ? (size_t)jamoOffsets[p] : p;
		};

		thread_local vector<size_t> bytePositions;
		thread_local u16string u16str;
		try
		{
			u16str = utf8To16(str, bytePositions);
		}
		catch (const UnicodeException&)
		{
			return;
		}
		if (u16str.empty()) return;
		bytePositions.emplace_back(str.size());

		TokenResult res;
		try
		{
			res = kiwi.analyze(u16str, AnalyzeOption{});
		}
		catch (const Exception&)
		{
			return;
		}

		const TokenInfo* prevToken = nullptr;
		POSTag prevTag = POSTag::unknown;
		size_t prevEnd = 0;
		for (auto& token : res.first)
		{
			const bool split = isPretokenizingBoundary(pretokenizeOption, prevTag, token.tag, token.str.empty() ? 0 : token.str[0]);

			if (split)
			{
				const size_t codaOrigin = bytePositions[token.position];
				const bool splittableCoda = useJamoAlphabet
					&& (!token.str.empty() && isHangulCoda(token.str[0]))
					&& (extractHangulCoda(u16str[token.position]) == token.str[0])
					&& (prevToken && !prevToken->str.empty())
					&& (prevToken->str.back() != u16str[token.position]);
				const size_t v = splittableCoda
					? (size_t)jamoOffsets[codaOrigin] + hangulOnsetNucleusBytes
					: mapOffset(bytePositions[prevEnd]);

				// '가능하답니다'와 같은 패턴에서는 prevEnd의 순서가 역전되어 나타날 수 있기 때문에
				// 항상 boundariesOut이 단조증가하도록 보장하는 게 필요하다.
				while (!boundariesOut.empty() && boundariesOut.back() > v) boundariesOut.pop_back();
				if (boundariesOut.empty() || boundariesOut.back() != v) boundariesOut.emplace_back(v);
			}
			prevToken = &token;
			prevTag = token.tag;
			prevEnd = token.endPos();
		}
	}

	// Returns the text the spans index into: `str`, or its jamo decomposition in `jamoBuf` when useJamoAlphabet is on,
	// in which case `jamoOffsets` is left holding the byte map from `str` to `jamoBuf`.
	static const string* extractChunkSpans(
		vector<pair<size_t, size_t>>& chunksOut,
		string& jamoBuf,
		vector<uint32_t>& jamoOffsets,
		const string& str,
		size_t maxDigitLength = 0,
		size_t maxRepeatLength = 0,
		size_t maxWhitespaceRepeatLength = 0,
		bool useJamoAlphabet = false,
		PretokenizeOption pretokenizeOption = PretokenizeOption::none,
		const Kiwi* kiwi = nullptr
	)
	{
		chunksOut.clear();
		if (str.empty())
		{
			jamoBuf.clear();
			jamoOffsets.clear();
			return &str;
		}

		if (useJamoAlphabet) decomposeHangul(str, jamoBuf, &jamoOffsets);
		else jamoOffsets.clear();
		const string* const text = useJamoAlphabet ? &jamoBuf : &str;

		auto mapOffset = [&](size_t p) -> size_t
		{
			return useJamoAlphabet ? (size_t)jamoOffsets[p] : p;
		};

		thread_local vector<size_t> boundaries;
		boundaries.clear();
		if (kiwi && pretokenizeOption != PretokenizeOption::none)
		{
			collectMorphemeBoundaries(boundaries, str, pretokenizeOption, useJamoAlphabet, jamoOffsets, *kiwi);
		}

		size_t boundaryCursor = 0;
		auto emitChunk = [&](size_t startOrig, size_t length)
		{
			size_t start = mapOffset(startOrig);
			const size_t end = mapOffset(startOrig + length);
			while (boundaryCursor < boundaries.size() && boundaries[boundaryCursor] <= start) ++boundaryCursor;
			while (boundaryCursor < boundaries.size() && boundaries[boundaryCursor] < end)
			{
				chunksOut.emplace_back(start, boundaries[boundaryCursor] - start);
				start = boundaries[boundaryCursor];
				++boundaryCursor;
			}
			chunksOut.emplace_back(start, end - start);
		};

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
					emitChunk(start, i - start);
					continue;
				}
				else if ((c1 == 'r' && c2 == 'e') || (c1 == 'v' && c2 == 'e') || (c1 == 'l' && c2 == 'l'))
				{
					i += 3;
					emitChunk(start, i - start);
					continue;
				}
			}

			// 2. Spaces
			if (classifyCodepoint(decodeUtf8Codepoint(str, i).value) == ChrClass::space)
			{
				size_t j = i;
				size_t lastSpace = i;

				char32_t repeatedCp = 0;
				size_t repeatLength = 0;
				bool truncated = false;
				while (j < n)
				{
					const auto cp = decodeUtf8Codepoint(str, j);
					if (classifyCodepoint(cp.value) != ChrClass::space) break;
					if (cp.value == repeatedCp)
					{
						if (maxWhitespaceRepeatLength && repeatLength >= maxWhitespaceRepeatLength)
						{
							truncated = true;
							break;
						}
						++repeatLength;
					}
					else
					{
						repeatedCp = cp.value;
						repeatLength = 1;
					}
					lastSpace = j;
					j += cp.size;
				}

				if (truncated)
				{
					emitChunk(start, j - start);
					i = j;
					continue;
				}

				if (j == n)
				{
					emitChunk(start, j - start);
					i = j;
					continue;
				}

				if (str[lastSpace] == ' ')
				{
					if (j - i > 1)
					{
						emitChunk(start, (j - 1) - start);
						i = j - 1;
						continue;
					}
				}
				else
				{
					emitChunk(start, j - start);
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
					const size_t maxRun = (cls == ChrClass::number && maxDigitLength)
						? maxDigitLength : (size_t)-1;
					size_t runLength = 1;

					char32_t repeatedCp = cp.value;
					size_t repeatLength = 1;
					i += cp.size;
					while (i < n && runLength < maxRun)
					{
						const auto next = decodeUtf8Codepoint(str, i);
						if (classifyCodepoint(next.value) != cls) break;
						if (next.value == repeatedCp)
						{
							if (maxRepeatLength && repeatLength >= maxRepeatLength) break;
							++repeatLength;
						}
						else
						{
							repeatedCp = next.value;
							repeatLength = 1;
						}
						i += next.size;
						++runLength;
					}
				}
			}

			emitChunk(start, i - start);
		}
		return text;
	}

	template<bool largeCounter>
	struct BpeTokenizerTrainerImpl final : BpeTokenizerTrainer::Impl
	{
		WordCountMap<largeCounter> wordCounts;

		// ---- chunk extraction --------------------------------------------------

		static void addChunksTo(const string& str,
								bool addPrefixSpace,
								size_t maxDigitLength,
								size_t maxRepeatLength,
								size_t maxWhitespaceRepeatLength,
								bool useJamoAlphabet,
								PretokenizeOption pretokenizeOption,
								const Kiwi* kiwi,
		                        WordCountMap<largeCounter>& wc)
		{
			const string* workStr = &str;
			if (addPrefixSpace && !str.empty() && str[0] != ' ')
			{
				thread_local string prefixBuf;
				prefixBuf.assign(1, ' ');
				prefixBuf += str;
				workStr = &prefixBuf;
			}
			thread_local vector<pair<size_t, size_t>> spanBuf;
			thread_local string jamoBuf;
			thread_local vector<uint32_t> jamoOffsetBuf;
			const string* text = extractChunkSpans(spanBuf, jamoBuf, jamoOffsetBuf, *workStr,
				maxDigitLength, maxRepeatLength, maxWhitespaceRepeatLength, useJamoAlphabet, pretokenizeOption, kiwi);
			for (auto& span : spanBuf)
				wc.add(string_view(text->data() + span.first, span.second));
		}

		// ---- addSentences ------------------------------------------------------

		size_t addSentences(size_t& sentenceCount, const BpeTrainerConfig& config,
							const Kiwi* kiwi,
		                    const BpeTokenizerTrainerEventCallback& callback,
		                    const function<string()>& feeder) override
		{
			emitEvent(callback, BpeTokenizerTrainerEvent::pretokenizeBegin, 0, 0);

			size_t totalCount = 0;
			vector<string> batch;
			const size_t maxWorkerCount = config.numThreads == (size_t)-1 ? thread::hardware_concurrency() : config.numThreads;

			vector<WordCountMap<largeCounter>> localCounts(maxWorkerCount);
			deque<future<void>> futures;

			auto processRange = [&](vector<string>&& data, WordCountMap<largeCounter>& targetCounts)
			{
				for (auto& s : data)
				{
					addChunksTo(s, config.addPrefixSpace, config.maxDigitLength, config.maxRepeatLength, config.maxWhitespaceRepeatLength, config.useJamoAlphabet != JamoAlphabet::none, config.pretokenizeOption, kiwi, targetCounts);
				}
			};

			unique_ptr<utils::ThreadPool> pool;
			if (maxWorkerCount > 0)
			{
				pool = make_unique<utils::ThreadPool>(maxWorkerCount);
			}

			// An empty string ends the input; `feeder` is never called again after it.
			bool eof = false;
			while (!eof)
			{
				batch.clear();
				batch.reserve(config.batchSize);
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

				totalCount += batch.size();
				sentenceCount += batch.size();

				if (!pool)
				{
					processRange(move(batch), wordCounts);
				}
				else
				{
					while (futures.size() >= pool->size() * 4)
					{
						auto f = move(futures.front());
						f.get();
						futures.pop_front();
					}

				  	futures.emplace_back(pool->enqueue([&, batch = move(batch)](size_t tid) mutable
					{
						processRange(move(batch), localCounts[tid]);
					}));
				}

				emitEvent(callback, BpeTokenizerTrainerEvent::pretokenizeProgress, totalCount, 0);
			}

			if (pool)
			{
				while (!futures.empty())
				{
					auto f = move(futures.front());
					f.get();
					futures.pop_front();
				}

				for (auto& localCount : localCounts)
				{
					localCount.forEach([&](string_view sv, size_t count)
					{
						wordCounts.add(sv, count);
					});
					localCount = WordCountMap<largeCounter>{};
				}
			}

			emitEvent(callback, BpeTokenizerTrainerEvent::pretokenizeEnd, totalCount, totalCount);
			return totalCount;
		}

		// ---- build -------------------------------------------------------------

		BpeTokenizer build(const BpeTrainerConfig& config,
		                   const BpeTokenizerTrainerEventCallback& callback) const override
		{
			const size_t targetVocabSize = config.vocabSize > 0 ? config.vocabSize : (size_t)-1;
			const size_t reportedTotal = config.vocabSize;

			vector<string> vocab;
			// Cap the up-front reservation so an absurd vocabSize doesn't allocate before training.
			vocab.reserve(min(config.vocabSize ? config.vocabSize : (size_t)256, (size_t)1 << 20));
			unordered_map<string, uint32_t> vocabToId;

			for (int i = 0; i < 256; ++i)
			{
				string s(1, (char)(unsigned char)i);
				vocabToId.emplace(s, (uint32_t)i);
				vocab.push_back(move(s));
			}

			// Bloom-style mask of the word's token ids: a clear bit proves a token is absent, a set bit only suggests it.
			auto tokenBit = [](uint32_t t) -> uint64_t { return 1ull << (t & 63); };

			struct Word { vector<uint32_t> tokens; size_t count = 0; uint64_t mask = 0; };

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
			if (words.size() > (uint32_t)-1)
				throw length_error{ "BpeTokenizerTrainer: more than 2^32 distinct chunks" };

			auto makeKey = [](uint32_t a, uint32_t b) -> uint64_t {
				return ((uint64_t)a << 32) | (uint64_t)b;
			};

			unordered_map<uint64_t, MergeRule> merges;

			vector<string> pinnedAlphabet = config.additionalAlphabet;
			if (config.useJamoAlphabet == JamoAlphabet::modern_only)
			{
				for (size_t i = 0; i < hangulOnsetCount; ++i)  pinnedAlphabet.emplace_back(utf8FromCode(hangulOnsetFirst + (char32_t)i));
				for (size_t i = 0; i < hangulVowelCount; ++i) pinnedAlphabet.emplace_back(utf8FromCode(hangulVowelFirst + (char32_t)i));
				for (size_t i = 0; i < hangulCodaCount; ++i)  pinnedAlphabet.emplace_back(utf8FromCode(hangulCodaFirst + (char32_t)i));
			}
			else if (config.useJamoAlphabet == JamoAlphabet::all)
			{
				for (char32_t c = 0x1100; c <= 0x11FF; ++c) pinnedAlphabet.emplace_back(utf8FromCode(c));
				for (char32_t c = 0xA960; c <= 0xA97F; ++c) pinnedAlphabet.emplace_back(utf8FromCode(c));
				for (char32_t c = 0xD7B0; c <= 0xD7FF; ++c) pinnedAlphabet.emplace_back(utf8FromCode(c));
			}

			for (const auto& entry : pinnedAlphabet)
			{
				// A single byte is in the alphabet already, so there is nothing to pin.
				uint32_t cur = (uint32_t)(unsigned char)entry[0];
				for (size_t i = 1; i < entry.size(); ++i)
				{
					const uint64_t key = makeKey(cur, (uint32_t)(unsigned char)entry[i]);
					const auto itMerge = merges.find(key);
					if (itMerge != merges.end())
					{
						cur = itMerge->second.newId;
						continue;
					}

					string prefix = entry.substr(0, i + 1);
					uint32_t newId = (uint32_t)vocab.size();
					vocabToId.emplace(prefix, newId);
					vocab.push_back(move(prefix));
					merges.emplace(key, MergeRule{ (uint32_t)merges.size(), newId });
					cur = newId;
				}
			}

			if (!merges.empty())
			{
				// Apply pinned entries in the order encode() will (lowest rank first, left to right) so both agree on overlaps.
				for (auto& w : words)
				{
					auto& tokens = w.tokens;
					while (tokens.size() >= 2)
					{
						uint32_t bestRank = (uint32_t)-1;
						for (size_t i = 0; i + 1 < tokens.size(); ++i)
						{
							const unordered_map<uint64_t, MergeRule>::const_iterator it = merges.find(makeKey(tokens[i], tokens[i + 1]));
							if (it != merges.end() && it->second.rank < bestRank) bestRank = it->second.rank;
						}
						if (bestRank == (uint32_t)-1) break;

						size_t out = 0;
						for (size_t i = 0; i < tokens.size(); )
						{
							if (i + 1 < tokens.size())
							{
								const auto it = merges.find(makeKey(tokens[i], tokens[i + 1]));
								if (it != merges.end() && it->second.rank == bestRank)
								{
									tokens[out++] = it->second.newId;
									i += 2;
									continue;
								}
							}
							tokens[out++] = tokens[i++];
						}
						tokens.resize(out);
					}

					w.mask = 0;
					for (uint32_t t : tokens) w.mask |= tokenBit(t);
				}
			}

			// Count carried by this pair's single authoritative heap entry (always >= `count`). It makes the heap a lazy
			// decrease-key: push only when the count rises above it, and drop popped entries that don't match it.
			struct PairStat { size_t count = 0; size_t pushed = 0; };
			unordered_map<uint64_t, PairStat> pairCounts;
			for (const auto& w : words)
				for (size_t i = 0; i + 1 < w.tokens.size(); ++i)
					pairCounts[makeKey(w.tokens[i], w.tokens[i + 1])].count += w.count;

			// tokenToWords[t]: ascending, deduplicated indices of the words containing token t.
			vector<vector<uint32_t>> tokenToWords(vocab.size());
			{
				vector<char> seen(vocab.size(), 0);
				vector<uint32_t> distinctCount(vocab.size(), 0);
				for (const auto& w : words)
				{
					for (uint32_t t : w.tokens) if (!seen[t]) { seen[t] = true; ++distinctCount[t]; }
					for (uint32_t t : w.tokens) seen[t] = false;
				}
				for (size_t t = 0; t < vocab.size(); ++t) tokenToWords[t].reserve(distinctCount[t]);
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

			// Pairs rejected by maxTokenLength, kept apart instead of zeroing pairCounts whose next decrement would wrap around.
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
				if (itCount == pairCounts.end()) continue;
				PairStat& stat = itCount->second;
				// Superseded duplicate.
				if (count != stat.pushed) continue;
				if (stat.count < config.minPairFrequency)
				{
					// Retire the entry so that a later rise pushes a fresh one.
					stat.pushed = 0;
					continue;
				}
				if (stat.count != count)
				{
					// Over-stated snapshot: correct it and re-insert. No entry under-states its pair's count,
					// so the first entry that matches is the true maximum.
					stat.pushed = stat.count;
					maxHeap.push({ stat.count, key });
					continue;
				}

				const uint32_t id1 = (uint32_t)(key >> 32);
				const uint32_t id2 = (uint32_t)(key & 0xFFFFFFFF);

				if (vocab[id1].size() + vocab[id2].size() > config.maxTokenLength)
				{
					blockedPairs.insert(key);
					// Never push again; the pair stays adjacent inside words, so its count keeps moving.
					stat.pushed = (size_t)-1;
					continue;
				}

				string newStr = vocab[id1] + vocab[id2];
				uint32_t newId;
				const auto itVocab = vocabToId.find(newStr);
				if (itVocab != vocabToId.end())
				{
					// Two pairs can produce the same string. Reuse the id: save() keys the vocab by string, so a duplicate would break loading.
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

				// newId differs from id1 and id2, so `candidates` isn't appended to here and the reference stays valid.
				// Words that no longer contain id1 are compacted out while walking.
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
						// A token is provably absent. Keep the entry while id1's bit is set, since only a clear bit proves id1 is gone.
						if (w.mask & bit1) candidates[keptCount++] = wIdx;
						continue;
					}

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

					// Withdraw the old pairs,
					for (size_t i = 0; i + 1 < w.tokens.size(); ++i)
					{
						const uint64_t pKey = makeKey(w.tokens[i], w.tokens[i + 1]);
						const auto it = pairCounts.find(pKey);
						assert(it != pairCounts.end());
						if (it == pairCounts.end()) continue;
						// Counts are unsigned; drop the entry instead of ever wrapping.
						if (it->second.count <= w.count) { pairCounts.erase(it); continue; }
						// No push: a decrement never makes a new maximum, and the stale entry is corrected when it surfaces.
						it->second.count -= w.count;
					}

					// rewrite the word in place (it never grows), rebuilding the mask since a merge can retire tokens,
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

					// and re-deposit the new pairs.
					for (size_t j = 0; j + 1 < w.tokens.size(); ++j)
					{
						const uint64_t pKey = makeKey(w.tokens[j], w.tokens[j + 1]);
						PairStat& ps = pairCounts[pKey];
						ps.count += w.count;
						// Push only when the count outgrows this pair's existing entry.
						if (ps.count >= config.minPairFrequency && ps.count > ps.pushed)
						{
							ps.pushed = ps.count;
							maxHeap.push({ ps.count, pKey });
						}
					}

					// Keep the entry only while id1 survives in the rewritten word.
					if ((w.mask & bit1) && find(w.tokens.begin(), w.tokens.end(), id1) != w.tokens.end())
						candidates[keptCount++] = wIdx;

					tokenToWords[newId].push_back(wIdx);
				}
				candidates.resize(keptCount);

				// Re-arm any residue, since decrements no longer push. The strict decrease keeps this loop finite.
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

			return BpeTokenizer(move(vocab), move(merges), config.addPrefixSpace, config.useJamoAlphabet != JamoAlphabet::none);
		}
	};

	static void buildByteToCharPos(vector<uint32_t>& byteToCharPos, const string& str)
	{
		byteToCharPos.clear();
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
	}

	namespace
	{
		struct TokenSpan
		{
			uint32_t id;
			uint32_t start;
			uint32_t end;
		};

		// Scratch buffers reused across calls; thread_local because encode() is const and may run concurrently.
		struct EncodeScratch
		{
			string prefixed;
			vector<pair<size_t, size_t>> spans;
			string jamoBuf;
			vector<uint32_t> jamoOffsets;
			vector<uint32_t> byteToCharPos;
			vector<TokenSpan> tokens;
			vector<uint32_t> ranks, newIds;
		};
	}

	// Impossible id pair (it would need a 2^32-entry vocabulary), doubling as the empty-slot marker.
	static constexpr uint64_t emptyMergeKey = (uint64_t)-1;

	static inline uint64_t mergeHash(uint64_t k)
	{
		k *= 0x9E3779B97F4A7C15ull;
		return k ^ (k >> 29);
	}

	void BpeTokenizer::buildMergeTable()
	{
		mergeTable.clear();
		mergeTableMask = 0;
		if (merges.empty()) return;

		size_t capacity = 16;
		while (capacity < merges.size() * 2) capacity <<= 1; // load factor stays under 1/2
		mergeTable.assign(capacity, MergeSlot{ emptyMergeKey, MergeRule{} });
		mergeTableMask = capacity - 1;

		for (const auto& kv : merges)
		{
			if (kv.first == emptyMergeKey) continue;
			size_t idx = mergeHash(kv.first) & mergeTableMask;
			while (mergeTable[idx].key != emptyMergeKey) idx = (idx + 1) & mergeTableMask;
			mergeTable[idx] = { kv.first, kv.second };
		}
	}

	const MergeRule* BpeTokenizer::findMerge(uint32_t a, uint32_t b) const
	{
		if (mergeTable.empty()) return nullptr;
		const uint64_t key = ((uint64_t)a << 32) | (uint64_t)b;
		size_t idx = mergeHash(key) & mergeTableMask;
		for (;;)
		{
			const auto& slot = mergeTable[idx];
			if (slot.key == key) return &slot.rule;
			if (slot.key == emptyMergeKey) return nullptr;
			idx = (idx + 1) & mergeTableMask;
		}
	}

	bool BpeTokenizer::ready() const
	{
		return !vocab.empty();
	}

	void BpeTokenizer::encode(vector<uint32_t>& out, const string& str, vector<pair<uint32_t, uint32_t>>* offset, bool offsetInChrLevel) const
	{
		if (str.empty()) return;

		thread_local EncodeScratch scratch;

		const string* work = &str;
		bool prependedSpace = false;
		if (addPrefixSpace && str[0] != ' ')
		{
			scratch.prefixed.assign(1, ' ');
			scratch.prefixed += str;
			work = &scratch.prefixed;
			prependedSpace = true;
		}
		const string& workStr = *work;

		// Decompose exactly when the vocabulary was trained on jamo, or none of its merges would fire.
		const string& text = *extractChunkSpans(scratch.spans, scratch.jamoBuf, scratch.jamoOffsets,
			workStr, 0, 0, 0, nfdForHangul);

		// Maps a position in the decomposed text back to workStr: a start rounds down to its syllable, an end rounds up.
		// Tokens come out in position order, so a forward-only cursor suffices.
		const auto& jamoOffsets = scratch.jamoOffsets;
		size_t cursorPos = 0, cursorEnd = 0;
		auto seekJamo = [&](uint32_t j)
		{
			while (cursorPos < workStr.size() && jamoOffsets[cursorEnd] <= j)
			{
				cursorPos = cursorEnd;
				if (cursorPos >= workStr.size()) break;
				cursorEnd = cursorPos + 1;
				while (cursorEnd < workStr.size() && jamoOffsets[cursorEnd] == jamoOffsets[cursorPos]) ++cursorEnd;
			}
		};
		auto mapStart = [&](uint32_t j) -> uint32_t
		{
			if (!nfdForHangul) return j;
			seekJamo(j);
			return (uint32_t)cursorPos;
		};
		auto mapEnd = [&](uint32_t j) -> uint32_t
		{
			if (!nfdForHangul) return j;
			seekJamo(j);
			if (cursorPos >= workStr.size()) return (uint32_t)workStr.size();
			// Exactly on the syllable's first jamo means the token stopped in front of it.
			return jamoOffsets[cursorPos] == j ? (uint32_t)cursorPos : (uint32_t)cursorEnd;
		};
		if (nfdForHangul && !workStr.empty())
		{
			cursorEnd = 1;
			while (cursorEnd < workStr.size() && jamoOffsets[cursorEnd] == jamoOffsets[0]) ++cursorEnd;
		}

		auto makeKey = [](uint32_t a, uint32_t b) -> uint64_t {
			return ((uint64_t)a << 32) | (uint64_t)b;
		};

		auto& byteToCharPos = scratch.byteToCharPos;
		if (offset && offsetInChrLevel)
		{
			buildByteToCharPos(byteToCharPos, str);
		}

		// Rank and resulting id of the merge for the pair at i, looked up again only when a merge disturbs that pair.
		constexpr uint32_t noRank = UINT32_MAX;      // no merge rule for this pair
		constexpr uint32_t dirtyRank = UINT32_MAX - 1; // pair changed, rank not looked up yet
		auto& tokens = scratch.tokens;
		auto& ranks = scratch.ranks;
		auto& newIds = scratch.newIds;

		auto lookupPair = [&](size_t i)
		{
			const MergeRule* rule = findMerge(tokens[i].id, tokens[i + 1].id);
			if (!rule)
			{
				ranks[i] = noRank;
			}
			else
			{
				ranks[i] = rule->rank;
				newIds[i] = rule->newId;
			}
		};

		// No reserve(): a caller accumulating several calls into one vector would reallocate on every call.
		for (const auto& span : scratch.spans)
		{
			size_t k = span.second;
			tokens.resize(k);
			for (size_t i = 0; i < k; ++i)
			{
				const size_t origIdx = span.first + i;
				tokens[i] = { (uint32_t)(unsigned char)text[origIdx], (uint32_t)origIdx, (uint32_t)(origIdx + 1) };
			}

			if (k >= 2)
			{
				ranks.resize(k);
				newIds.resize(k);
				for (size_t i = 0; i + 1 < k; ++i) lookupPair(i);
				ranks[k - 1] = noRank;

				for (;;)
				{
					uint32_t bestRank = noRank;
					size_t bestIdx = 0;
					for (size_t i = 0; i + 1 < k; ++i)
					{
						if (ranks[i] == dirtyRank) lookupPair(i);
						if (ranks[i] < bestRank) { bestRank = ranks[i]; bestIdx = i; }
					}
					if (bestRank == noRank) break;
					const uint32_t bestNewId = newIds[bestIdx];

					// Compact in place; a merge only invalidates the pair it forms and the one before it.
					size_t o = 0;
					for (size_t i = 0; i < k; )
					{
						if (i + 1 < k && ranks[i] == bestRank)
						{
							tokens[o] = { bestNewId, tokens[i].start, tokens[i + 1].end };
							ranks[o] = dirtyRank;
							if (o) ranks[o - 1] = dirtyRank;
							++o;
							i += 2;
						}
						else
						{
							tokens[o] = tokens[i];
							ranks[o] = ranks[i];
							newIds[o] = newIds[i];
							++o;
							++i;
						}
					}
					k = o;
					ranks[k - 1] = noRank;
					if (k < 2) break;
				}
			}

			for (size_t i = 0; i < k; ++i)
			{
				const auto& tok = tokens[i];
				out.push_back(tok.id);
				if (offset)
				{
					const uint32_t tStart = mapStart(tok.start);
					const uint32_t tEnd   = mapEnd(tok.end);

					uint32_t sStart = prependedSpace ? (tStart > 0 ? tStart - 1 : 0) : tStart;
					uint32_t sEnd   = prependedSpace ? (tEnd   > 0 ? tEnd   - 1 : 0) : tEnd;

					if (sStart > str.size()) sStart = (uint32_t)str.size();
					if (sEnd   > str.size()) sEnd   = (uint32_t)str.size();

					if (offsetInChrLevel)
					{
						offset->emplace_back(byteToCharPos[sStart], byteToCharPos[sEnd]);
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

	// GPT-2 bytes-to-unicode: printable bytes map to themselves, the other 68 to U+0100..U+0143.
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
		// "nfd_for_hangul" is our own normalizer type; only our load() needs to understand it.
		if (nfdForHangul) j["normalizer"] = { {"type", "nfd_for_hangul"} };
		else j["normalizer"] = nullptr;
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

		bool addPrefixSpace = false;
		if (j.contains("pre_tokenizer") && !j["pre_tokenizer"].is_null())
		{
			auto& pt = j["pre_tokenizer"];
			if (pt.contains("add_prefix_space") && pt["add_prefix_space"].is_boolean())
				addPrefixSpace = pt["add_prefix_space"].get<bool>();
		}

		// Other normalizers are ignored.
		bool nfdForHangul = false;
		if (j.contains("normalizer") && j["normalizer"].is_object())
		{
			auto& nm = j["normalizer"];
			if (nm.contains("type") && nm["type"].is_string())
				nfdForHangul = nm["type"].get<string>() == "nfd_for_hangul";
		}

		auto& model = j["model"];

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

		unordered_map<string, uint32_t> vocabToId;
		vocabToId.reserve(vocab.size());
		for (size_t i = 0; i < vocab.size(); ++i)
			vocabToId[vocab[i]] = (uint32_t)i;

		auto makeKey = [](uint32_t a, uint32_t b) -> uint64_t {
			return ((uint64_t)a << 32) | (uint64_t)b;
		};

		// Index in the list is the rank.
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

		return BpeTokenizer(move(vocab), move(merges), addPrefixSpace, nfdForHangul);
	}

	

	BpeTokenizerTrainer::BpeTokenizerTrainer(const BpeTrainerConfig& config, const Kiwi* kiwi, BpeTokenizerTrainerEventCallback callback)
		: config(config), kiwi(kiwi), callback(move(callback))
	{
		if (config.vocabSize && config.vocabSize < 256)
			throw invalid_argument("BpeTokenizerTrainer::vocabSize must be zero or at least 256");
		if (!config.maxTokenLength)
			throw invalid_argument("BpeTokenizerTrainer::maxTokenLength must be positive");
		if (!config.batchSize)
			throw invalid_argument("BpeTokenizerTrainer::batchSize must be positive");
		for (const auto& entry : config.additionalAlphabet)
			if (entry.empty())
				throw invalid_argument("BpeTokenizerTrainer::additionalAlphabet must not contain an empty string");
		if ((uint8_t)config.useJamoAlphabet > (uint8_t)JamoAlphabet::all)
			throw invalid_argument("BpeTokenizerTrainer::useJamoAlphabet must be none, modern_only or all");

		if (config.pretokenizeOption != PretokenizeOption::none && !kiwi)
			throw invalid_argument("BpeTokenizerTrainer::pre-tokenization requires a valid Kiwi instance");

		if (config.largeCounter)
			impl = make_unique<BpeTokenizerTrainerImpl<true>>();
		else
			impl = make_unique<BpeTokenizerTrainerImpl<false>>();
	}

	BpeTokenizerTrainer::~BpeTokenizerTrainer() = default;

	size_t BpeTokenizerTrainer::addSentences(const std::function<std::string()>& feeder)
	{
		return impl->addSentences(sentenceCount, config, kiwi, callback, feeder);
	}

	size_t BpeTokenizerTrainer::addSentences(const std::function<std::u16string()>& feeder)
	{
		return impl->addSentences(sentenceCount, config, kiwi, callback, [&]()
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
