#include <cmath>
#include <memory>
#include <fstream>
#include <sstream>
#include <iostream>
#include <streambuf>
#include <vector>
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
	kiwi_ss(vector<pair<size_t, size_t>>&& o) : vector{ std::move(o) }
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
		: tokenizer{ std::move(_tokenizer) }
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

kiwi_builder_h kiwi_builder_init(const char* model_path, int num_threads, int options, int enabled_dialects)
{
	try
	{
		BuildOption buildOption = (BuildOption)(options & 0xFF);
		const auto mtMask = options & (KIWI_BUILD_MODEL_TYPE_LARGEST | KIWI_BUILD_MODEL_TYPE_KNLM | KIWI_BUILD_MODEL_TYPE_SBG | KIWI_BUILD_MODEL_TYPE_CONG | KIWI_BUILD_MODEL_TYPE_CONG_GLOBAL);
		const ModelType modelType = (mtMask == KIWI_BUILD_MODEL_TYPE_LARGEST) ? ModelType::largest
			: (mtMask == KIWI_BUILD_MODEL_TYPE_KNLM) ? ModelType::knlm
			: (mtMask == KIWI_BUILD_MODEL_TYPE_SBG) ? ModelType::sbg
			: (mtMask == KIWI_BUILD_MODEL_TYPE_CONG) ? ModelType::cong
			: (mtMask == KIWI_BUILD_MODEL_TYPE_CONG_GLOBAL) ? ModelType::congGlobal
			: ModelType::none;
		return (kiwi_builder_h)new KiwiBuilder{ model_path, (size_t)num_threads, buildOption, modelType, (Dialect)enabled_dialects };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

// Custom istream implementation that uses the kiwi_stream_object_t
class CStreamAdapter : public std::istream {
private:
    class CStreamBuf : public std::streambuf {
    private:
        kiwi_stream_object_t stream_obj;
        std::vector<char> buffer;
        static constexpr const size_t buffer_size = 8192;
        
    public:
        CStreamBuf(const kiwi_stream_object_t& obj) : stream_obj(obj), buffer(buffer_size) {
            setg(buffer.data(), buffer.data(), buffer.data());
        }
        
        ~CStreamBuf() {
            if (stream_obj.close) {
                stream_obj.close(stream_obj.user_data);
            }
        }
        
    protected:
        int underflow() override {
            if (gptr() < egptr()) {
                return traits_type::to_int_type(*gptr());
            }
            
            if (!stream_obj.read) {
                return traits_type::eof();
            }
            
            size_t bytes_read = stream_obj.read(stream_obj.user_data, buffer.data(), buffer_size);
            if (bytes_read == 0) {
                return traits_type::eof();
            }
            
            setg(buffer.data(), buffer.data(), buffer.data() + bytes_read);
            return traits_type::to_int_type(*gptr());
        }
        
        pos_type seekoff(off_type off, std::ios_base::seekdir way, std::ios_base::openmode) override {
            if (!stream_obj.seek) {
                return pos_type(-1);
            }
            
            int whence;
            switch (way) {
                case std::ios_base::beg: whence = 0; break; // SEEK_SET
                case std::ios_base::cur: whence = 1; break; // SEEK_CUR
                case std::ios_base::end: whence = 2; break; // SEEK_END
                default: return pos_type(-1);
            }
            
            long long new_pos = stream_obj.seek(stream_obj.user_data, off, whence);
            if (new_pos == -1) {
                return pos_type(-1);
            }
            
            // Reset buffer after seek
            setg(buffer.data(), buffer.data(), buffer.data());
            return pos_type(new_pos);
        }
        
        pos_type seekpos(pos_type sp, std::ios_base::openmode which) override {
            return seekoff(sp, std::ios_base::beg, which);
        }
    };
    
    CStreamBuf buf;
    
public:
    CStreamAdapter(const kiwi_stream_object_t& obj) : std::istream(&buf), buf(obj) {}
};

kiwi_builder_h kiwi_builder_init_stream(kiwi_stream_object_t (*stream_object_factory)(const char* filename), int num_threads, int options, int enabled_dialects)
{
	try
	{
		BuildOption buildOption = (BuildOption)(options & 0xFF);
		const auto mtMask = options & (KIWI_BUILD_MODEL_TYPE_LARGEST | KIWI_BUILD_MODEL_TYPE_KNLM | KIWI_BUILD_MODEL_TYPE_SBG | KIWI_BUILD_MODEL_TYPE_CONG | KIWI_BUILD_MODEL_TYPE_CONG_GLOBAL);
		const ModelType modelType = (mtMask == KIWI_BUILD_MODEL_TYPE_LARGEST) ? ModelType::largest
			: (mtMask == KIWI_BUILD_MODEL_TYPE_KNLM) ? ModelType::knlm
			: (mtMask == KIWI_BUILD_MODEL_TYPE_SBG) ? ModelType::sbg
			: (mtMask == KIWI_BUILD_MODEL_TYPE_CONG) ? ModelType::cong
			: (mtMask == KIWI_BUILD_MODEL_TYPE_CONG_GLOBAL) ? ModelType::congGlobal
			: ModelType::none;
		
		// Create C++ StreamProvider that uses the stream object factory
		KiwiBuilder::StreamProvider cppStreamProvider = [stream_object_factory](const std::string& filename) -> std::unique_ptr<std::istream>
		{
			kiwi_stream_object_t stream_obj = stream_object_factory(filename.c_str());
			if (!stream_obj.read) {
				return nullptr; // Invalid stream object (missing required read function)
			}
			
			return std::make_unique<CStreamAdapter>(stream_obj);
		};
		
		return (kiwi_builder_h)new KiwiBuilder{ cppStreamProvider, (size_t)num_threads, buildOption, modelType, (Dialect)enabled_dialects};
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
		vector<tuple<u16string, POSTag, uint8_t>> analyzed(size);
		vector<pair<size_t, size_t>> p_positions;

		for (int i = 0; i < size; ++i)
		{
			analyzed[i] = make_tuple(utf8To16(analyzed_morphs[i]), parse_tag(analyzed_pos[i]), undefSenseId);
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

		return new kiwi_ws{ std::move(res), {} };
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
		return new kiwi_ws{ std::move(res), {} };
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

		return new kiwi_ws{ std::move(res), {} };
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
		return new kiwi_ws{ std::move(res), {} };
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

void kiwi_set_global_config(kiwi_h handle, kiwi_config_t config)
{
	if (!handle) return;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		KiwiConfig kconfig{
			!!config.integrate_allomorph,
			config.cut_off_threshold,
			config.unk_form_score_scale,
			config.unk_form_score_bias,
			config.space_penalty,
			config.typo_cost_weight,
			config.max_unk_form_size,
			config.space_tolerance,
		};
		kiwi->setGlobalConfig(kconfig);
	}
	catch (...)
	{
		currentError = current_exception();
	}
}

kiwi_config_t kiwi_get_global_config(kiwi_h handle)
{
	kiwi_config_t config{};
	if (!handle) return config;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		KiwiConfig kconfig = kiwi->getGlobalConfig();
		config.integrate_allomorph = kconfig.integrateAllomorph;
		config.cut_off_threshold = kconfig.cutOffThreshold;
		config.unk_form_score_scale = kconfig.unkFormScoreScale;
		config.unk_form_score_bias = kconfig.unkFormScoreBias;
		config.space_penalty = kconfig.spacePenalty;
		config.typo_cost_weight = kconfig.typoCostWeight;
		config.max_unk_form_size = kconfig.maxUnkFormSize;
		config.space_tolerance = kconfig.spaceTolerance;
	}
	catch (...)
	{
		currentError = current_exception();
	}
	return config;
}

void kiwi_set_option(kiwi_h handle, int option, int value)
{
	if (!handle) return;
	Kiwi* kiwi = (Kiwi*)handle;
	switch (option)
	{
	case KIWI_NUM_THREADS:
		currentError = make_exception_ptr(runtime_error{ "Cannot modify the number of threads." });
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
	case KIWI_NUM_THREADS:
		return kiwi->getNumThreads();
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

inline AnalyzeOption toAnalyzeOption(kiwi_analyze_option_t option)
{
	return AnalyzeOption{
		(Match)option.match_options,
		option.blocklist ? &option.blocklist->morphemes : nullptr,
		!!option.open_ending,
		(Dialect)option.allowed_dialects,
		option.dialect_cost
	};
}

kiwi_res_h kiwi_analyze_w(kiwi_h handle, const kchar16_t * text, int top_n, kiwi_analyze_option_t option, kiwi_pretokenized_h pretokenized)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		return new kiwi_res{ kiwi->analyze(
			(const char16_t*)text, top_n, 
			toAnalyzeOption(option),
			pretokenized ? *pretokenized : std::vector<PretokenizedSpan>{}
		), {} };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

kiwi_res_h kiwi_analyze(kiwi_h handle, const char* text, int top_n, kiwi_analyze_option_t option, kiwi_pretokenized_h pretokenized)
{
	if (!handle) return nullptr;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		return new kiwi_res{ kiwi->analyze(
			text, top_n, 
			toAnalyzeOption(option),
			pretokenized ? *pretokenized : std::vector<PretokenizedSpan>{}
		),{} };
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_analyze_mw(kiwi_h handle, kiwi_reader_w_t reader, kiwi_receiver_t receiver, void * userData, int top_n, kiwi_analyze_option_t option)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		int reader_idx = 0, receiver_idx = 0;
		kiwi->analyze(top_n, [&]() -> u16string
		{
			u16string buf;
			buf.resize((*reader)(reader_idx, nullptr, userData));
			if (buf.empty()) return {};
			(*reader)(reader_idx++, (kchar16_t*)&buf[0], userData);
			return buf;
		}, [&](vector<TokenResult>&& res)
		{
			auto result = new kiwi_res{ std::move(res), {} };
			(*receiver)(receiver_idx++, result, userData);
		}, toAnalyzeOption(option));
		return reader_idx;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_analyze_m(kiwi_h handle, kiwi_reader_t reader, kiwi_receiver_t receiver, void * userData, int top_n, kiwi_analyze_option_t option)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	Kiwi* kiwi = (Kiwi*)handle;
	try
	{
		int reader_idx = 0, receiver_idx = 0;
		kiwi->analyze(top_n, [&]() -> u16string
		{
			string buf;
			buf.resize((*reader)(reader_idx, nullptr, userData));
			if (buf.empty()) return {};
			(*reader)(reader_idx++, &buf[0], userData);
			return utf8To16(buf);
		}, [&](vector<TokenResult>&& res)
		{
			auto result = new kiwi_res{ std::move(res),{} };
			(*receiver)(receiver_idx++, result, userData);
		}, toAnalyzeOption(option));
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
		if (tokenized_res) *tokenized_res = new kiwi_res{ std::move(tokenized), {} };
		return new kiwi_ss{ std::move(sent_ranges) };
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
		if (tokenized_res) *tokenized_res = new kiwi_res{ std::move(tokenized), {} };
		return new kiwi_ss{ std::move(sent_ranges) };
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

const char* kiwi_tag_to_string(kiwi_h handle, uint8_t pos_tag)
{
	return tagToString((POSTag)pos_tag);
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

int kiwi_res_morpheme_id(kiwi_res_h result, int index, int num, kiwi_h kiwi_handle)
{
	try
	{
		if (!result || !kiwi_handle) return KIWIERR_INVALID_HANDLE;
		if (index < 0 || index >= result->first.size() || num < 0 || num >= result->first[index].first.size()) return KIWIERR_INVALID_INDEX;
		return ((Kiwi*)kiwi_handle)->morphToId(result->first[index].first[num].morph);
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
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

int kiwi_find_morphemes(kiwi_h handle, const char* form, const char* tag, int sense_id, unsigned int* morph_ids, int max_count)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		Kiwi* kiwi = (Kiwi*)handle;
		auto ret = kiwi->findMorphemes(utf8To16(form), tag ? parse_tag(tag) : POSTag::unknown, (uint8_t)sense_id);
		max_count = min((int)ret.size(), max_count);
		for (int i = 0; i < max_count; ++i)
		{
			morph_ids[i] = kiwi->morphToId(ret[i]);
		}
		return max_count;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_find_morphemes_with_prefix(kiwi_h handle, const char* form_prefix, const char* tag, int sense_id, unsigned int* morph_ids, int max_count)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		Kiwi* kiwi = (Kiwi*)handle;
		auto ret = kiwi->findMorphemesWithPrefix(max_count, utf8To16(form_prefix), tag ? parse_tag(tag) : POSTag::unknown, (uint8_t)sense_id);
		max_count = min((int)ret.size(), max_count);
		for (int i = 0; i < max_count; ++i)
		{
			morph_ids[i] = kiwi->morphToId(ret[i]);
		}
		return max_count;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

kiwi_morpheme_t kiwi_get_morpheme_info(kiwi_h handle, unsigned int morph_id)
{
	kiwi_morpheme_t info{ 0, };
	if (!handle) return info;
	try
	{
		auto* morpheme = ((Kiwi*)handle)->idToMorph(morph_id);
		if (!morpheme) return info;
		info.tag = (uint8_t)morpheme->tag;
		info.sense_id = morpheme->senseId;
		info.user_score = morpheme->userScore;
		info.lm_morpheme_id = morpheme->lmMorphemeId;
		info.orig_morpheme_id = morpheme->origMorphemeId;
		info.dialect = (uint16_t)morpheme->dialect;
		return info;
	}
	catch (...)
	{
		currentError = current_exception();
		return info;
	}
}

const kchar16_t* kiwi_get_morpheme_form_w(kiwi_h handle, unsigned int morph_id)
{
	if (!handle) return nullptr;
	try
	{
		auto* morpheme = ((Kiwi*)handle)->idToMorph(morph_id);
		if (!morpheme) return nullptr;
		auto form = joinHangul(morpheme->getForm());
		auto* buf = new kchar16_t[form.size() + 1];
		wcsncpy((wchar_t*)buf, (const wchar_t*)form.c_str(), form.size() + 1);
		return buf;
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

const char* kiwi_get_morpheme_form(kiwi_h handle, unsigned int morph_id)
{
	if (!handle) return nullptr;
	try
	{
		auto* morpheme = ((Kiwi*)handle)->idToMorph(morph_id);
		if (!morpheme) return nullptr;
		auto form = utf16To8(joinHangul(morpheme->getForm()));
		auto* buf = new char[form.size() + 1];
		strncpy(buf, form.c_str(), form.size() + 1);
		return buf;
	}
	catch (...)
	{
		currentError = current_exception();
		return nullptr;
	}
}

int kiwi_free_morpheme_form(const char* form)
{
	try
	{
		delete[] form;
		return 0;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_cong_most_similar_words(kiwi_h handle, unsigned int morph_id, kiwi_similarity_pair_t* output, int top_n)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		Kiwi* kiwi = (Kiwi*)handle;
		auto cong = dynamic_cast<const lm::CoNgramModelBase*>(kiwi->getLangModel());
		if (!cong) throw invalid_argument{ "The given kiwi object does not have CoNgram language model." };
		size_t cnt = cong->mostSimilarWords(morph_id, top_n, (pair<uint32_t, float>*)output);
		return (int)cnt;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

float kiwi_cong_similarity(kiwi_h handle, unsigned int morph_id1, unsigned int morph_id2)
{
	if (!handle) return NAN;
	try
	{
		Kiwi* kiwi = (Kiwi*)handle;
		auto cong = dynamic_cast<const lm::CoNgramModelBase*>(kiwi->getLangModel());
		if (!cong) throw invalid_argument{ "The given kiwi object does not have CoNgram language model." };
		return cong->wordSimilarity(morph_id1, morph_id2);
	}
	catch (...)
	{
		currentError = current_exception();
		return NAN;
	}
}

int kiwi_cong_most_similar_contexts(kiwi_h handle, unsigned int context_id, kiwi_similarity_pair_t* output, int top_n)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		Kiwi* kiwi = (Kiwi*)handle;
		auto cong = dynamic_cast<const lm::CoNgramModelBase*>(kiwi->getLangModel());
		if (!cong) throw invalid_argument{ "The given kiwi object does not have CoNgram language model." };
		size_t cnt = cong->mostSimilarContexts(context_id, top_n, (pair<uint32_t, float>*)output);
		return (int)cnt;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

float kiwi_cong_context_similarity(kiwi_h handle, unsigned int context_id1, unsigned int context_id2)
{
	if (!handle) return NAN;
	try
	{
		Kiwi* kiwi = (Kiwi*)handle;
		auto cong = dynamic_cast<const lm::CoNgramModelBase*>(kiwi->getLangModel());
		if (!cong) throw invalid_argument{ "The given kiwi object does not have CoNgram language model." };
		return cong->contextSimilarity(context_id1, context_id2);
	}
	catch (...)
	{
		currentError = current_exception();
		return NAN;
	}
}

int kiwi_cong_predict_words_from_context(kiwi_h handle, unsigned int context_id, kiwi_similarity_pair_t* output, int top_n)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		Kiwi* kiwi = (Kiwi*)handle;
		auto cong = dynamic_cast<const lm::CoNgramModelBase*>(kiwi->getLangModel());
		if (!cong) throw invalid_argument{ "The given kiwi object does not have CoNgram language model." };
		size_t cnt = cong->predictWordsFromContext(context_id, top_n, (pair<uint32_t, float>*)output);
		return (int)cnt;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

int kiwi_cong_predict_words_from_context_diff(kiwi_h handle, unsigned int context_id, unsigned int bg_context_id, float weight, kiwi_similarity_pair_t* output, int top_n)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		Kiwi* kiwi = (Kiwi*)handle;
		auto cong = dynamic_cast<const lm::CoNgramModelBase*>(kiwi->getLangModel());
		if (!cong) throw invalid_argument{ "The given kiwi object does not have CoNgram language model." };
		size_t cnt = cong->predictWordsFromContextDiff(context_id, bg_context_id, weight, top_n, (pair<uint32_t, float>*)output);
		return (int)cnt;
	}
	catch (...)
	{
		currentError = current_exception();
		return KIWIERR_FAIL;
	}
}

unsigned int kiwi_cong_to_context_id(kiwi_h handle, const unsigned int* morph_ids, int size)
{
	if (!handle) return 0;
	try
	{
		Kiwi* kiwi = (Kiwi*)handle;
		auto cong = dynamic_cast<const lm::CoNgramModelBase*>(kiwi->getLangModel());
		if (!cong) throw invalid_argument{ "The given kiwi object does not have CoNgram language model." };
		return cong->toContextId(morph_ids, size);
	}
	catch (...)
	{
		currentError = current_exception();
		return 0;
	}
}

int kiwi_cong_from_context_id(kiwi_h handle, unsigned int context_id, unsigned int* morph_ids, int max_size)
{
	if (!handle) return KIWIERR_INVALID_HANDLE;
	try
	{
		Kiwi* kiwi = (Kiwi*)handle;
		auto cong = dynamic_cast<const lm::CoNgramModelBase*>(kiwi->getLangModel());
		if (!cong) throw invalid_argument{ "The given kiwi object does not have CoNgram language model." };
		auto& vec = cong->getContextWordMapCached();
		if ((size_t)context_id >= vec.size())
		{
			throw out_of_range{ "Invalid context ID." };
		}

		max_size = min((int)vec[context_id].size(), max_size);
		for (int i = 0; i < max_size; ++i)
		{
			morph_ids[i] = vec[context_id][i];
		}
		return max_size;
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
		auto found = handle->inst->findMorphemes(utf8To16(form), ptag);
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
		auto found = handle->inst->findMorphemes((const char16_t*)form, ptag);
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
			handle->cachedTokenIds = std::move(tokenIds);
			handle->cachedOffset = std::move(offset);
			return handle->cachedTokenIds.size();
		}

		if (handle->encodeLastText == strHash)
		{
			tokenIds = std::move(handle->cachedTokenIds);
			offset = std::move(handle->cachedOffset);
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
			handle->cachedText = std::move(decoded);
			return handle->cachedText.size();
		}

		if (handle->decodeLastTokenIds == hash)
		{
			decoded = std::move(handle->cachedText);
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
