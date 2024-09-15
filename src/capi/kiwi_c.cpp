#include <cmath>
#include <memory>
#include <fstream>
#include <kiwi/Kiwi.h>
#include <kiwi/SwTokenizer.h>
#include <kiwi/capi.h>

using namespace std;
using namespace kiwi;

const char* kiwi_version()
{
	return KIWI_VERSION_STRING;
}

struct ResultBuffer
{
	vector<string> stringBuf;
};

struct kiwi_res : public pair<vector<TokenResult>, ResultBuffer>
{
	using pair<vector<TokenResult>, ResultBuffer>::pair;
};

struct kiwi_ws : public pair<vector<WordInfo>, ResultBuffer>
{
	using pair<vector<WordInfo>, ResultBuffer>::pair;
};

struct kiwi_ss : public vector<pair<size_t, size_t>>
{
	kiwi_ss(vector<pair<size_t, size_t>>&& o) : vector{ move(o) }
	{
	}
};

struct kiwi_joiner : public tuple<cmb::AutoJoiner, string, u16string>
{
	using tuple<cmb::AutoJoiner, string, u16string>::tuple;
};

struct kiwi_typo : public TypoTransformer
{
};

struct kiwi_morphset
{
	Kiwi* inst = nullptr;
	std::unordered_set<const Morpheme*> morphemes;

	kiwi_morphset(Kiwi* _inst) : inst{ _inst }
	{
	}
};

struct kiwi_pretokenized : public std::vector<PretokenizedSpan>
{
};

struct kiwi_swtokenizer
{
	SwTokenizer tokenizer;
	size_t encodeLastText = 0;
	size_t decodeLastTokenIds = 0;

	vector<uint32_t> cachedTokenIds;
	vector<pair<uint32_t, uint32_t>> cachedOffset;
	string cachedText;

	kiwi_swtokenizer(SwTokenizer&& _tokenizer)
		: tokenizer{move(_tokenizer)}
	{}
};

thread_local exception_ptr currentError;

inline POSTag parse_tag(const char* pos)
{
	auto u16 = utf8To16(pos);
	transform(u16.begin(), u16.end(), u16.begin(), static_cast<int(*)(int)>(toupper));
	auto ret = toPOSTag(u16);
	if (ret == POSTag::max) throw invalid_argument{ string{"Unknown POSTag : "} + pos };
	return ret;
}

const char* kiwi_error()
{
	if (currentError)
	{
		try
		{
			rethrow_exception(currentError);
		}
		catch (const exception& e)
		{
			return e.what();
		}
	}
	return nullptr;
}

void kiwi_clear_error()
{
	currentError = {};
}

kiwi_builder_h kiwi_builder_init(const char* model_path, int num_threads, int options)
{
	try
	{
		BuildOption buildOption = (BuildOption)(options & 0xFF);
		bool useSBG = !!(options & KIWI_BUILD_MODEL_TYPE_SBG);
		return (kiwi_builder_h)new KiwiBuilder{ model_path, (size_t)num_threads, buildOption, useSBG};
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_builder_close(kiwi_builder_h handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	auto* kb = (KiwiBuilder*)handle;
	try
	{
		delete kb;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_builder_add_word(kiwi_builder_h handle, const char* word, const char* pos, float score)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	auto* kb = (KiwiBuilder*)handle;
	try
	{
		if (kb->addWord(utf8To16(word), parse_tag(pos), score).second) return 0;
		return KIWIERR_FAIL;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_builder_add_alias_word(kiwi_builder_h handle, const char* alias, const char* pos, float score, const char* orig_word)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	auto* kb = (KiwiBuilder*)handle;
	try
	{
		if (kb->addWord(utf8To16(alias), parse_tag(pos), score, utf8To16(orig_word)).second) return 0;
		return KIWIERR_FAIL;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_builder_add_pre_analyzed_word(kiwi_builder_h handle, const char* form, int size, const char** analyzed_morphs, const char** analyzed_pos, float score, const int* positions)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	auto* kb = (KiwiBuilder*)handle;
	try
	{
		if (size < 0)
		{
			throw invalid_argument{ "`size` must be positive integer." };
		}
		vector<pair<u16string, POSTag>> analyzed(size);
		vector<pair<size_t, size_t>> p_positions;

		for (int i = 0; i < size; ++i)
		{
			analyzed[i].first = utf8To16(analyzed_morphs[i]);
			analyzed[i].second = parse_tag(analyzed_pos[i]);
		}

		if (positions)
		{
			p_positions.resize(size);
			for (int i = 0; i < size; ++i)
			{
				p_positions[i].first = positions[i * 2];
				p_positions[i].second = positions[i * 2 + 1];
			}
		}

		if (kb->addPreAnalyzedWord(utf8To16(form), analyzed, p_positions, score)) return 0;
		return KIWIERR_FAIL;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_builder_add_rule(kiwi_builder_h handle, const char* pos, kiwi_builder_replacer_t replacer, void* user_data, float score)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	auto* kb = (KiwiBuilder*)handle;
	try
	{
		return kb->addRule(parse_tag(pos), [&](const u16string& str) 
		{
			auto s8 = utf16To8(str);
			size_t buf_len = replacer(s8.c_str(), s8.size(), nullptr, user_data);
			vector<char> buf(buf_len);
			replacer(s8.c_str(), s8.size(), buf.data(), user_data);
			return utf8To16(buf.data());
		}, score).size();
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_builder_load_dict(kiwi_builder_h handle, const char* dictPath)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	auto* kb = (KiwiBuilder*)handle;
	try
	{
		return kb->loadDictionary(dictPath);
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

kiwi_ws_h kiwi_builder_extract_words(kiwi_builder_h handle, kiwi_reader_t reader, void* userData, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return nullptr;
	auto* kb = (KiwiBuilder*)handle;
	try
	{
		int idx = 0;
		auto res = kb->extractWords([&]()
		{
			idx = 0;
			return [&]() -> u16string
			{
				string buf;
				buf.resize((*reader)(idx, nullptr, userData));
				if (buf.empty()) return {};
				(*reader)(idx, &buf[0], userData);
				++idx;
				return utf8To16(buf);
			};
		}, minCnt, maxWordLen, minScore, posThreshold);

		return new kiwi_ws{ move(res), {} };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

kiwi_ws_h kiwi_builder_extract_add_words(kiwi_builder_h handle, kiwi_reader_t reader, void* userData, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return nullptr;
	auto* kb = (KiwiBuilder*)handle;
	try
	{
		int idx = 0;
		auto res = kb->extractAddWords([&]()
		{
			idx = 0;
			return [&]() -> u16string
			{
				string buf;
				buf.resize((*reader)(idx, nullptr, userData));
				if (buf.empty()) return {};
				(*reader)(idx, &buf[0], userData);
				++idx;
				return utf8To16(buf);
			};
		}, minCnt, maxWordLen, minScore, posThreshold);
		return new kiwi_ws{ move(res), {} };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

kiwi_ws_h kiwi_builder_extract_words_w(kiwi_builder_h handle, kiwi_reader_w_t reader, void* userData, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return nullptr;
	auto* kb = (KiwiBuilder*)handle;
	try
	{
		int idx = 0;
		auto res = kb->extractWords([&]()
		{
			idx = 0;
			return [&]()->u16string
			{
				u16string buf;
				buf.resize((*reader)(idx, nullptr, userData));
				if (buf.empty()) return {};
				(*reader)(idx, (kchar16_t*)&buf[0], userData);
				++idx;
				return buf;
			};
		}, minCnt, maxWordLen, minScore, posThreshold);

		return new kiwi_ws{ move(res), {} };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

kiwi_ws_h kiwi_builder_extract_add_words_w(kiwi_builder_h handle, kiwi_reader_w_t reader, void* userData, int minCnt, int maxWordLen, float minScore, float posThreshold)
{
	if (!handle) return nullptr;
	auto* kb = (KiwiBuilder*)handle;
	try
	{
		int idx = 0;
		auto res = kb->extractAddWords([&]()
		{
			idx = 0;
			return [&]()->u16string
			{
				u16string buf;
				buf.resize((*reader)(idx, nullptr, userData));
				if (buf.empty()) return {};
				(*reader)(idx, (kchar16_t*)&buf[0], userData);
				++idx;
				return buf;
			};
		}, minCnt, maxWordLen, minScore, posThreshold);
		return new kiwi_ws{ move(res), {} };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

kiwi_h kiwi_builder_build(kiwi_builder_h handle, kiwi_typo_h typos, float typo_cost_threshold)
{
	if (!handle) return nullptr;
	auto* kb = (KiwiBuilder*)handle;
	try
	{
		const TypoTransformer* tt = &getDefaultTypoSet(DefaultTypoSet::withoutTypo);
		if (typos)
		{
			tt = typos;
		}
		return (kiwi_h)new Kiwi{ kb->build(*tt, typo_cost_threshold) };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

kiwi_typo_h kiwi_typo_init()
{
	try
	{
		return new kiwi_typo;
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

kiwi_typo_h kiwi_typo_get_basic()
{
	return kiwi_typo_get_default(KIWI_TYPO_BASIC_TYPO_SET);
}

kiwi_typo_h kiwi_typo_get_default(int kiwi_typo_set)
{
	try
	{
		return (kiwi_typo_h)&getDefaultTypoSet((DefaultTypoSet)kiwi_typo_set);
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_typo_add(kiwi_typo_h handle, const char** orig, int orig_size, const char** error, int error_size, float cost, int condition)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		vector<u16string> origs, errors;
		for (int i = 0; i < orig_size; ++i)
		{
			origs.emplace_back(utf8To16(orig[i]));
		}
		for (int i = 0; i < error_size; ++i)
		{
			errors.emplace_back(utf8To16(error[i]));
		}
		for (auto& o : origs)
		{
			for (auto& e : errors)
			{
				handle->addTypo(o, e, cost, (CondVowel)condition);
			}
		}
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return -1;
	}
}

kiwi_typo_h kiwi_typo_copy(kiwi_typo_h handle)
{
	if (!handle) return nullptr;
	try
	{
		return new kiwi_typo{ *handle };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_typo_update(kiwi_typo_h handle, kiwi_typo_h src)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		handle->update(*src);
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return -1;
	}
}

int kiwi_typo_scale_cost(kiwi_typo_h handle, float cost)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		handle->scaleCost(cost);
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return -1;
	}
}

int kiwi_typo_set_continual_typo_cost(kiwi_typo_h handle, float threshold)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		handle->setContinualTypoCost(threshold);
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return -1;
	}
}

int kiwi_typo_set_lengthening_typo_cost(kiwi_typo_h handle, float threshold)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		handle->setLengtheningTypoCost(threshold);
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return -1;
	}
}

int kiwi_typo_close(kiwi_typo_h handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		delete handle;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return -1;
	}
}

kiwi_h kiwi_init(const char * modelPath, int num_threads, int options)
{
	try
	{
		return (kiwi_h)new Kiwi{ KiwiBuilder{ modelPath, (size_t)num_threads, (BuildOption)options }.build() };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

void kiwi_set_option(kiwi_h handle, int option, int value)
{
	if (!handle) return;
	Kiwi* kiwi = (Kiwi*)handle;
	switch (option)
	{
	case KIWI_BUILD_INTEGRATE_ALLOMORPH:
		kiwi->setIntegrateAllomorph(!!value);
		break;
	case KIWI_NUM_THREADS:
		currentError = make_exception_ptr(runtime_error{ "Cannot modify the number of threads." });
		break;
	case KIWI_MAX_UNK_FORM_SIZE:
		kiwi->setMaxUnkFormSize(value);
		break;
	case KIWI_SPACE_TOLERANCE:
		kiwi->setSpaceTolerance(value);
		break;
	default:
		currentError = make_exception_ptr(invalid_argument{ "Invalid option value: " + to_string(option)});
		break;
	}
}

int kiwi_get_option(kiwi_h handle, int option)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	switch (option)
	{
	case KIWI_BUILD_INTEGRATE_ALLOMORPH:
		return kiwi->getIntegrateAllomorph() ? 1 : 0;
	case KIWI_NUM_THREADS:
		return kiwi->getNumThreads();
	case KIWI_MAX_UNK_FORM_SIZE:
		return kiwi->getMaxUnkFormSize();
	case KIWI_SPACE_TOLERANCE:
		return kiwi->getSpaceTolerance();
	default:
		currentError = make_exception_ptr(invalid_argument{ "Invalid option value: " + to_string(option) });
		break;
	}
	return KIWIERR_INVALID_INDEX;
}

void kiwi_set_option_f(kiwi_h handle, int option, float value)
{
	if (!handle) return;
	Kiwi* kiwi = (Kiwi*)handle;
	switch (option)
	{
	case KIWI_CUT_OFF_THRESHOLD:
		kiwi->setCutOffThreshold(value);
		break;
	case KIWI_UNK_FORM_SCORE_SCALE:
		kiwi->setUnkScoreScale(value);
		break;
	case KIWI_UNK_FORM_SCORE_BIAS:
		kiwi->setUnkScoreBias(value);
		break;
	case KIWI_SPACE_PENALTY:
		kiwi->setSpacePenalty(value);
		break;
	case KIWI_TYPO_COST_WEIGHT:
		kiwi->setTypoCostWeight(value);
		break;
	default:
		currentError = make_exception_ptr(invalid_argument{ "Invalid option value: " + to_string(option) });
		break;
	}
}

float kiwi_get_option_f(kiwi_h handle, int option)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	switch (option)
	{
	case KIWI_CUT_OFF_THRESHOLD:
		return kiwi->getCutOffThreshold();
	case KIWI_UNK_FORM_SCORE_SCALE:
		return kiwi->getUnkScoreScale();
	case KIWI_UNK_FORM_SCORE_BIAS:
		return kiwi->getUnkScoreBias();
	case KIWI_SPACE_PENALTY:
		return kiwi->getSpacePenalty();
	case KIWI_TYPO_COST_WEIGHT:
		return kiwi->getTypoCostWeight();
	default:
		currentError = make_exception_ptr(invalid_argument{ "Invalid option value: " + to_string(option) });
		break;
	}
	return KIWIERR_INVALID_INDEX;
}

kiwi_morphset_h kiwi_new_morphset(kiwi_h handle)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		return new kiwi_morphset{ kiwi };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

kiwi_res_h kiwi_analyze_w(kiwi_h handle, const kchar16_t * text, int topN, int matchOptions, kiwi_morphset_h blocklilst, kiwi_pretokenized_h pretokenized)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		return new kiwi_res{ kiwi->analyze(
			(const char16_t*)text, topN, (Match)matchOptions, 
			blocklilst ? &blocklilst->morphemes : nullptr,
			pretokenized ? *pretokenized : std::vector<PretokenizedSpan>{}
		), {} };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

kiwi_res_h kiwi_analyze(kiwi_h handle, const char * text, int topN, int matchOptions, kiwi_morphset_h blocklilst, kiwi_pretokenized_h pretokenized)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		return new kiwi_res{ kiwi->analyze(
			text, topN, (Match)matchOptions, 
			blocklilst ? &blocklilst->morphemes : nullptr,
			pretokenized ? *pretokenized : std::vector<PretokenizedSpan>{}
		),{} };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_analyze_mw(kiwi_h handle, kiwi_reader_w_t reader, kiwi_receiver_t receiver, void * userData, int topN, int matchOptions, kiwi_morphset_h blocklilst)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		int reader_idx = 0, receiver_idx = 0;
		kiwi->analyze(topN, [&]() -> u16string
		{
			u16string buf;
			buf.resize((*reader)(reader_idx, nullptr, userData));
			if (buf.empty()) return {};
			(*reader)(reader_idx++, (kchar16_t*)&buf[0], userData);
			return buf;
		}, [&](vector<TokenResult>&& res)
		{
			auto result = new kiwi_res{ move(res), {} };
			(*receiver)(receiver_idx++, result, userData);
		}, (Match)matchOptions, blocklilst ? &blocklilst->morphemes : nullptr);
		return reader_idx;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_analyze_m(kiwi_h handle, kiwi_reader_t reader, kiwi_receiver_t receiver, void * userData, int topN, int matchOptions, kiwi_morphset_h blocklilst)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		int reader_idx = 0, receiver_idx = 0;
		kiwi->analyze(topN, [&]() -> u16string
		{
			string buf;
			buf.resize((*reader)(reader_idx, nullptr, userData));
			if (buf.empty()) return {};
			(*reader)(reader_idx++, &buf[0], userData);
			return utf8To16(buf);
		}, [&](vector<TokenResult>&& res)
		{
			auto result = new kiwi_res{ move(res),{} };
			(*receiver)(receiver_idx++, result, userData);
		}, (Match)matchOptions, blocklilst ? &blocklilst->morphemes : nullptr);
		return reader_idx;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

kiwi_ss_h kiwi_split_into_sents_w(kiwi_h handle, const kchar16_t* text, int matchOptions, kiwi_res_h* tokenized_res)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		vector<TokenResult> tokenized;
		if (tokenized_res) tokenized.resize(1);
		auto sent_ranges = kiwi->splitIntoSents((const char16_t*)text, (Match)matchOptions, tokenized_res ? tokenized.data() : nullptr);
		if (tokenized_res) *tokenized_res = new kiwi_res{ move(tokenized), {} };
		return new kiwi_ss{ move(sent_ranges) };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

kiwi_ss_h kiwi_split_into_sents(kiwi_h handle, const char* text, int matchOptions, kiwi_res_h* tokenized_res)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		vector<TokenResult> tokenized;
		if (tokenized_res) tokenized.resize(1);
		auto sent_ranges = kiwi->splitIntoSents(text, (Match)matchOptions, tokenized_res ? tokenized.data() : nullptr);
		if (tokenized_res) *tokenized_res = new kiwi_res{ move(tokenized), {} };
		return new kiwi_ss{ move(sent_ranges) };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

DECL_DLL kiwi_joiner_h kiwi_new_joiner(kiwi_h handle, int lm_search)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		return new kiwi_joiner{ kiwi->newJoiner(!!lm_search), string{}, u16string{} };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_close(kiwi_h handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		delete kiwi;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_res_size(kiwi_res_h result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		return result->first.size();
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

float kiwi_res_prob(kiwi_res_h result, int index)
{
	if (!result) return 0;
	try
	{
		if (index < 0 || index >= result->first.size()) return 0;
		return result->first[index].second;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_res_word_num(kiwi_res_h result, int index)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		if (index < 0 || index >= result->first.size()) return KIWIERR_INVALID_INDEX;
		return result->first[index].first.size();
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

const kiwi_token_info_t* kiwi_res_token_info(kiwi_res_h result, int index, int num)
{
	if (!result) return nullptr;
	try
	{
		if (index < 0 || index >= result->first.size() || num < 0 || num >= result->first[index].first.size()) return nullptr;
		return (const kiwi_token_info_t*)&result->first[index].first[num].position;
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

const kchar16_t * kiwi_res_form_w(kiwi_res_h result, int index, int num)
{
	if (!result) return nullptr;
	try
	{
		if (index < 0 || index >= result->first.size() || num < 0 || num >= result->first[index].first.size()) return nullptr;
		return (const kchar16_t*)result->first[index].first[num].str.c_str();
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

const kchar16_t * kiwi_res_tag_w(kiwi_res_h result, int index, int num)
{
	if (!result) return nullptr;
	try
	{
		if (index < 0 || index >= result->first.size() || num < 0 || num >= result->first[index].first.size()) return nullptr;
		return (const kchar16_t*)tagRToKString(result->first[index].first[num].str.back(), result->first[index].first[num].tag);
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

const char * kiwi_res_form(kiwi_res_h result, int index, int num)
{
	if (!result) return nullptr;
	try
	{
		if (index < 0 || index >= result->first.size() || num < 0 || num >= result->first[index].first.size()) return nullptr;
		result->second.stringBuf.emplace_back(utf16To8(result->first[index].first[num].str));
		return result->second.stringBuf.back().c_str();
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

const char * kiwi_res_tag(kiwi_res_h result, int index, int num)
{
	if (!result) return nullptr;
	try
	{
		if (index < 0 || index >= result->first.size() || num < 0 || num >= result->first[index].first.size()) return nullptr;
		return tagRToString(result->first[index].first[num].str.back(), result->first[index].first[num].tag);
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_res_position(kiwi_res_h result, int index, int num)
{
	if (!result) return -1;
	try
	{
		if (index < 0 || index >= result->first.size() || num < 0 || num >= result->first[index].first.size()) return -1;
		return result->first[index].first[num].position;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_res_length(kiwi_res_h result, int index, int num)
{
	if (!result) return -1;
	try
	{
		if (index < 0 || index >= result->first.size() || num < 0 || num >= result->first[index].first.size()) return -1;
		return result->first[index].first[num].length;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_res_word_position(kiwi_res_h result, int index, int num)
{
	if (!result) return -1;
	try
	{
		if (index < 0 || index >= result->first.size() || num < 0 || num >= result->first[index].first.size()) return -1;
		return result->first[index].first[num].wordPosition;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_res_sent_position(kiwi_res_h result, int index, int num)
{
	if (!result) return -1;
	try
	{
		if (index < 0 || index >= result->first.size() || num < 0 || num >= result->first[index].first.size()) return -1;
		return result->first[index].first[num].sentPosition;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

float kiwi_res_score(kiwi_res_h result, int index, int num)
{
	if (!result) return 0;
	try
	{
		if (index < 0 || index >= result->first.size() || num < 0 || num >= result->first[index].first.size()) return -1;
		return result->first[index].first[num].score;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

float kiwi_res_typo_cost(kiwi_res_h result, int index, int num)
{
	if (!result) return -1;
	try
	{
		if (index < 0 || index >= result->first.size() || num < 0 || num >= result->first[index].first.size()) return -1;
		return result->first[index].first[num].typoCost;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_res_close(kiwi_res_h result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		delete result;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_ws_size(kiwi_ws_h result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		return result->first.size();
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

const kchar16_t * kiwi_ws_form_w(kiwi_ws_h result, int index)
{
	if (!result) return nullptr;
	try
	{
		if (index < 0 || index >= result->first.size()) return nullptr;
		return (const kchar16_t*)result->first[index].form.c_str();
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

const char * kiwi_ws_form(kiwi_ws_h result, int index)
{
	if (!result) return nullptr;
	try
	{
		if (index < 0 || index >= result->first.size()) return nullptr;
		result->second.stringBuf.emplace_back(utf16To8(result->first[index].form));
		return result->second.stringBuf.back().c_str();
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

float kiwi_ws_score(kiwi_ws_h result, int index)
{
	if (!result) return NAN;
	try
	{
		if (index < 0 || index >= result->first.size()) return NAN;
		return result->first[index].score;
	}
	catch (...)
	{
		currentError = current_exception();
		return NAN;
	}
}

int kiwi_ws_freq(kiwi_ws_h result, int index)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		if (index < 0 || index >= result->first.size()) return KIWIERR_INVALID_INDEX;
		return result->first[index].freq;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

float kiwi_ws_pos_score(kiwi_ws_h result, int index)
{
	if (!result) return NAN;
	try
	{
		if (index < 0 || index >= result->first.size()) return NAN;
		return result->first[index].posScore[POSTag::nnp];
	}
	catch (...)
	{
		currentError = current_exception();
		return NAN;
	}
}

int kiwi_ws_close(kiwi_ws_h result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		delete result;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_ss_size(kiwi_ss_h result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		return result->size();
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_ss_begin_position(kiwi_ss_h result, int index)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		if (index < 0 || index >= result->size()) return KIWIERR_INVALID_INDEX;
		return (*result)[index].first;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_ss_end_position(kiwi_ss_h result, int index)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		if (index < 0 || index >= result->size()) return KIWIERR_INVALID_INDEX;
		return (*result)[index].second;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_ss_close(kiwi_ss_h result)
{
	if (!result) return KIWIERR_INVALID_HANDLE;
	try
	{
		delete result;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_joiner_add(kiwi_joiner_h handle, const char* form, const char* tag, int option)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		get<0>(*handle).add(utf8To16(form), parse_tag(tag), !!option);
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

const char* kiwi_joiner_get(kiwi_joiner_h handle)
{
	if (!handle) return nullptr;
	try
	{
		get<1>(*handle) = get<0>(*handle).getU8();
		return get<1>(*handle).c_str();
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

const kchar16_t* kiwi_joiner_get_w(kiwi_joiner_h handle)
{
	if (!handle) return nullptr;
	try
	{
		get<2>(*handle) = get<0>(*handle).getU16();
		return (const kchar16_t*)get<2>(*handle).c_str();
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_joiner_close(kiwi_joiner_h handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		delete handle;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_morphset_add(kiwi_morphset_h handle, const char* form, const char* tag)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		POSTag ptag = tag ? parse_tag(tag) : POSTag::unknown;
		auto found = handle->inst->findMorpheme(utf8To16(form), ptag);
		handle->morphemes.insert(found.begin(), found.end());
		return found.size();
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_morphset_add_w(kiwi_morphset_h handle, const kchar16_t* form, const char* tag)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		POSTag ptag = tag ? parse_tag(tag) : POSTag::unknown;
		auto found = handle->inst->findMorpheme((const char16_t*)form, ptag);
		handle->morphemes.insert(found.begin(), found.end());
		return found.size();
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_morphset_close(kiwi_morphset_h handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		delete handle;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

kiwi_swtokenizer_h kiwi_swt_init(const char* path, kiwi_h kiwi)
{
	if (!kiwi) return nullptr;
	try
	{
		std::ifstream ifs;
		return new kiwi_swtokenizer{ SwTokenizer::load(*(Kiwi*)kiwi, openFile(ifs, path)) };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_swt_encode(kiwi_swtokenizer_h handle, const char* text, int text_size, int* token_ids, int token_ids_buf_size, int* offsets, int offset_buf_size)
{
	if (!handle || !text) return KIWIERR_INVALID_HANDLE;
	try
	{
		vector<pair<uint32_t, uint32_t>> offset;
		vector<uint32_t> tokenIds;
		auto str = text_size >= 0 ? string{ text, text + text_size } : string{ text };
		auto strHash = hash<string>{}(str);

		if (!token_ids)
		{
			tokenIds = handle->tokenizer.encode(str, &offset);
			handle->encodeLastText = strHash;
			handle->cachedTokenIds = move(tokenIds);
			handle->cachedOffset = move(offset);
			return handle->cachedTokenIds.size();
		}

		if (handle->encodeLastText == strHash)
		{
			tokenIds = move(handle->cachedTokenIds);
			offset = move(handle->cachedOffset);
			handle->encodeLastText = 0;
		}
		else
		{
			tokenIds = handle->tokenizer.encode(str, offsets ? &offset : nullptr);
		}
		size_t o = min((size_t)token_ids_buf_size, tokenIds.size());
		memcpy(token_ids, tokenIds.data(), sizeof(int) * o);
		if (offsets) memcpy(offsets, offset.data(), sizeof(int) * min((size_t)offset_buf_size, offset.size() * 2));
		return o;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

inline size_t hashVectorInt(const int* data, int size) 
{
	size_t seed = size;
	for (int i = 0; i < size; ++i) 
	{
		seed ^= (uint32_t)data[i] + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}
	return seed;
}

int kiwi_swt_decode(kiwi_swtokenizer_h handle, const int* token_ids, int token_size, char* text, int text_buf_size)
{
	if (!handle || !token_ids) return KIWIERR_INVALID_HANDLE;
	try
	{
		string decoded;
		auto hash = hashVectorInt(token_ids, token_size);
		if (!text)
		{
			decoded = handle->tokenizer.decode((const uint32_t*)token_ids, token_size);
			handle->decodeLastTokenIds = hash;
			handle->cachedText = move(decoded);
			return handle->cachedText.size();
		}

		if (handle->decodeLastTokenIds == hash)
		{
			decoded = move(handle->cachedText);
			handle->decodeLastTokenIds = 0;
		}
		else
		{
			decoded = handle->tokenizer.decode((const uint32_t*)token_ids, token_size);
		}
		size_t o = min((size_t)text_buf_size, decoded.size());
		memcpy(text, decoded.data(), o);
		return o;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_swt_close(kiwi_swtokenizer_h handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		delete handle;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

kiwi_pretokenized_h kiwi_pt_init()
{
	try
	{
		return new kiwi_pretokenized{};
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_pt_add_span(kiwi_pretokenized_h handle, int begin, int end)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		if (begin < 0 || end < 0) return KIWIERR_INVALID_INDEX;
		handle->emplace_back(PretokenizedSpan{ (uint32_t)begin, (uint32_t)end });
		return handle->size() - 1;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_pt_add_token_to_span(kiwi_pretokenized_h handle, int span_id, const char* form, const char* tag, int begin, int end)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		if (begin < 0 || end < 0 || span_id >= handle->size()) return KIWIERR_INVALID_INDEX;
		(*handle)[span_id].tokenization.emplace_back(utf8To16(form), begin, end, parse_tag(tag));
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_pt_add_token_to_span_w(kiwi_pretokenized_h handle, int span_id, const kchar16_t* form, const char* tag, int begin, int end)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		if (begin < 0 || end < 0 || span_id >= handle->size()) return KIWIERR_INVALID_INDEX;
		(*handle)[span_id].tokenization.emplace_back((const char16_t*)form, begin, end, parse_tag(tag));
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_pt_close(kiwi_pretokenized_h handle)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		delete handle;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

const char* kiwi_get_script_name(uint8_t script)
{
	return getScriptName((ScriptType)script);
}
