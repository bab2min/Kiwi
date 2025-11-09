#include <kiwi/Kiwi.h>

#include <map>
#include <sstream>
#include <nlohmann/json.hpp>

#include <emscripten.h>
#include <emscripten/val.h>
#include <emscripten/bind.h>

using namespace kiwi;
using namespace nlohmann;


static std::map<int, Kiwi> instances;

int nextInstanceId() {
    static int id = 0;
    return id++;
}


static std::map<int, std::unordered_set<const Morpheme*>> morphemeSets;

int nextMorphemeSetId() {
    static int id = 0;
    return id++;
}


template<typename T>
inline T getAtOrDefault(const json& args, size_t index, const T& defaultValue) {
    return args.size() > index ? args.at(index).get<T>() : defaultValue;
}


inline std::unordered_set<const Morpheme*> parseMorphemeSet(const Kiwi& kiwi, const json& morphs) {
    std::unordered_set<const Morpheme*> set;

    for (const auto& morph : morphs) {
        const std::string form8 = morph["form"];
        const std::u16string form = utf8To16(form8);

        POSTag tag = POSTag::unknown;
        if (morph.contains("tag")) {
            const std::string tagStr8 = morph["tag"];
            const std::u16string tagStr = utf8To16(tagStr8);
            tag = toPOSTag(tagStr);
        }

        uint8_t senseId = undefSenseId;
        if (morph.contains("senseId")) {
            senseId = morph["senseId"];
        }

        auto matches = kiwi.findMorphemes(form, tag, senseId);
        set.insert(matches.begin(), matches.end());
    }

    return set;
}


class BlockListArg {
    std::unordered_set<const Morpheme*> tempSet;
    int blockListId;

public:
    BlockListArg(const Kiwi& kiwi, const json& args, size_t index) : blockListId(-1) {
        if (args.size() <= index) {
            return;
        }
        const auto& arg = args.at(index);
        if (arg.is_number_integer()) {
            blockListId = arg.get<int>();
        } else if (arg.is_array()) {
            tempSet = parseMorphemeSet(kiwi, arg);
        }
    }

    const std::unordered_set<const Morpheme*>* setPtr() const {
        if (blockListId >= 0) {
            return &morphemeSets[blockListId];
        }
        if (!tempSet.empty()) {
            return &tempSet;
        }
        return nullptr;
    }
};


std::vector<PretokenizedSpan> parsePretokenizedArg(const json& args, size_t index) {
    std::vector<PretokenizedSpan> spans;

    if (args.size() <= index) {
        return spans;
    }

    const json& arg = args.at(index);

    if (!arg.is_array()) {
        return spans;
    }

    for (const auto& span : arg) {
        const uint32_t start = span["start"];
        const uint32_t end = span["end"];

        std::vector<BasicToken> tokenization;

        for (const auto& token : span["tokenization"]) {
            const std::string form8 = token["form"];
            const std::u16string form = utf8To16(form8);

            const uint32_t start = token["start"];
            const uint32_t end = token["end"];
            POSTag tag = POSTag::unknown;
            if (token.contains("tag")) {
                const std::string tagStr8 = token["tag"];
                const std::u16string tagStr = utf8To16(tagStr8);
                tag = toPOSTag(tagStr);
            }

            tokenization.push_back(BasicToken{ form, start, end, tag });
        }

        spans.push_back(PretokenizedSpan{ start, end, tokenization });
    }

    return spans;
}


inline json serializeTokenInfo(const Kiwi& kiwi, const TokenInfo& tokenInfo) {
    return {
        { "str", utf16To8(tokenInfo.str) },
        { "position", tokenInfo.position },
        { "wordPosition", tokenInfo.wordPosition },
        { "sentPosition", tokenInfo.sentPosition },
        { "lineNumber", tokenInfo.lineNumber },
        { "length", tokenInfo.length },
        { "tag", tagToString(tokenInfo.tag) },
        { "score", tokenInfo.score },
        { "typoCost", tokenInfo.typoCost },
        { "typoFormId", tokenInfo.typoFormId },
        { "pairedToken", tokenInfo.pairedToken },
        { "subSentPosition", tokenInfo.subSentPosition },
        { "morphId", kiwi.morphToId(tokenInfo.morph) },
        { "senseId", tokenInfo.senseId },
        { "script", static_cast<uint8_t>(tokenInfo.script) },
        { "dialect", static_cast<uint16_t>(tokenInfo.dialect) },
    };
}

inline json serializeTokenInfoVec(const Kiwi& kiwi, const std::vector<TokenInfo>& tokenInfoVec) {
    json result = json::array();
    for (const TokenInfo& tokenInfo : tokenInfoVec) {
        result.push_back(serializeTokenInfo(kiwi, tokenInfo));
    }
    return result;
}

inline json serializeTokenResult(const Kiwi& kiwi, const TokenResult& tokenResult) {
    return {
        { "tokens", serializeTokenInfoVec(kiwi, tokenResult.first) },
        { "score", tokenResult.second },
    };
}

inline json serializeTokenResultVec(const Kiwi& kiwi, const std::vector<TokenResult>& tokenResultVec) {
    json result = json::array();
    for (const TokenResult& tokenResult : tokenResultVec) {
        result.push_back(serializeTokenResult(kiwi, tokenResult));
    }
    return result;
}


json version(const json& args) {
    return KIWI_VERSION_STRING;
}

json build(const json& args) {
    const int id = nextInstanceId();

    const json buildArgs = args[0];

    const size_t numThreads = 0;
    const auto modelTypeStr = buildArgs.value("modelType", "none");

    const ModelType modelType = (modelTypeStr == "none") ? ModelType::none 
        : (modelTypeStr == "largest") ? ModelType::largest
        : (modelTypeStr == "knlm") ? ModelType::knlm
        : (modelTypeStr == "sbg") ? ModelType::sbg
        : (modelTypeStr == "cong") ? ModelType::cong
        : (modelTypeStr == "cong-global") ? ModelType::congGlobal
        : ModelType::none;
    
    BuildOption buildOptions = BuildOption::none;
    if (buildArgs.value("integrateAllomorph", true)) {
        buildOptions |= BuildOption::integrateAllomorph;
    }
    if (buildArgs.value("loadDefaultDict", true)) {
        buildOptions |= BuildOption::loadDefaultDict;
    }
    if (buildArgs.value("loadTypoDict", true)) {
        buildOptions |= BuildOption::loadTypoDict;
    }
    if (buildArgs.value("loadMultiDict", true)) {
        buildOptions |= BuildOption::loadMultiDict;
    }
    auto enabledDialects = buildArgs.value("enabledDialects", "standard");

    KiwiBuilder builder = [&]() {
        // Check if streamProvider is available, if so use it
        if (buildArgs.contains("streamProvider")) {
            std::string streamProviderCallbackName = buildArgs["streamProvider"];
            
            // Create StreamProvider that calls JavaScript function
            KiwiBuilder::StreamProvider streamProvider = [streamProviderCallbackName](const std::string& filename) -> std::unique_ptr<std::istream>
            {
                try
                {
                    // Call JavaScript function by name
                    emscripten::val jsFunction = emscripten::val::global(streamProviderCallbackName.c_str());
                    if (jsFunction.isUndefined())
                    {
                        return nullptr;
                    }
                    
                    emscripten::val result = jsFunction(filename);
                    if (result.isNull() || result.isUndefined())
                    {
                        return nullptr;
                    }
                    
                    // Convert result to vector<char>
                    std::vector<char> buffer;
                    if (result.hasOwnProperty("length"))
                    {
                        const int length = result["length"].as<int>();
                        buffer.resize(length);
                        
                        for (int i = 0; i < length; ++i)
                        {
                            buffer[i] = result[i].as<unsigned char>();
                        }
                    }
                    else if (result.typeOf().as<std::string>() == "string")
                    {
                        std::string str = result.as<std::string>();
                        buffer.assign(str.begin(), str.end());
                    }
                    else
                    {
                        return nullptr;
                    }
                    
                    std::string data(buffer.begin(), buffer.end());
                    return std::make_unique<std::istringstream>(std::move(data));
                }
                catch (...)
                {
                    return nullptr;
                }
            };

            return KiwiBuilder{
                streamProvider,
                numThreads,
                buildOptions,
                modelType,
                parseDialects(enabledDialects),
            };
        }
        else
        {
            // Use traditional modelPath approach
            const std::string modelPath = buildArgs["modelPath"];
            return KiwiBuilder{
                modelPath,
                numThreads,
                buildOptions,
                modelType,
                parseDialects(enabledDialects),
            };
        }
    }();

    const auto userDicts = buildArgs.value("userDicts", json::array());
    for (const auto& pathJson : userDicts) {
        const std::string path = pathJson;
        builder.loadDictionary(path);
    }

    const auto userWords = buildArgs.value("userWords", json::array());
    for (const auto& word : userWords) {
        const std::string word8 = word["word"];
        const std::u16string word16 = utf8To16(word8);

        const std::string tag8 = word.value("tag", "NNG");
        const std::u16string tag16 = utf8To16(tag8);
        const POSTag tag = toPOSTag(tag16);

        const float score = word.value("score", 0.0f);

        if (word.contains("origWord")) {
            const std::string origWord8 = word["origWord"];
            const std::u16string origWord16 = utf8To16(origWord8);

            builder.addWord(word16, tag, score, origWord16);
        } else {
            builder.addWord(word16, tag, score);
        }
    }

    const auto preanalyzedWords = buildArgs.value("preanalyzedWords", json::array());
    for (const auto& preanalyzedWord : preanalyzedWords) {
        const std::string form8 = preanalyzedWord["form"];
        const std::u16string form = utf8To16(form8);
        const float score = preanalyzedWord.value("score", 0.0f);

        std::vector<std::tuple<std::u16string, POSTag, uint8_t>> analyzed;
        std::vector<std::pair<size_t, size_t>> positions;

        for (const auto& analyzedToken : preanalyzedWord["analyzed"]) {
            const std::string form8 = analyzedToken["form"];
            const std::u16string form = utf8To16(form8);

            const std::string tag8 = analyzedToken["tag"];
            const std::u16string tag16 = utf8To16(tag8);
            const POSTag tag = toPOSTag(tag16);

            const uint8_t senseId = analyzedToken.value("senseId", undefSenseId);
            analyzed.emplace_back(form, tag, senseId);

            if (analyzedToken.contains("start") && analyzedToken.contains("end")) {
                const size_t start = analyzedToken["start"];
                const size_t end = analyzedToken["end"];
                positions.emplace_back(start, end);
            }
        }

        builder.addPreAnalyzedWord(form, analyzed, positions, score);
    }

    const auto typos = buildArgs.value("typos", json(nullptr));
    const float typoCostThreshold = buildArgs.value("typoCostThreshold", 2.5f);

    if (typos.is_null()) {
        instances.emplace(id, builder.build(DefaultTypoSet::withoutTypo, typoCostThreshold));
    } else if (typos.is_string()) {
        DefaultTypoSet typoSet = DefaultTypoSet::withoutTypo;
        const std::string typosStr = typos.get<std::string>();

        if (typosStr == "basic") {
            typoSet = DefaultTypoSet::basicTypoSet;
        } else if (typosStr == "continual") {
            typoSet = DefaultTypoSet::continualTypoSet;
        } else if (typosStr == "basicWithContinual") {
            typoSet = DefaultTypoSet::basicTypoSetWithContinual;
        }

        instances.emplace(id, builder.build(typoSet, typoCostThreshold));
    } else {
        TypoTransformer typoTransformer;

        for (const auto& def : typos.value("defs", json::array())) {
            const float cost = def.value("cost", 1.0f);
            
            CondVowel condVowel = CondVowel::none;
            const std::string condVowelStr = def.value("condVowel", "none");

            if (condVowelStr == "any") {
                condVowel = CondVowel::any;
            } else if (condVowelStr == "vowel") {
                condVowel = CondVowel::vowel;
            } else if (condVowelStr == "applosive") {
                condVowel = CondVowel::applosive;
            }

            for (const auto& orig8 : def["orig"]) {
                const auto orig16 = utf8To16(orig8);
                for (const auto& error8 : def["error"]) {
                    typoTransformer.addTypo(orig16, utf8To16(error8), cost, condVowel);
                }
            }
        }

        const float continualTypoCost = typos.value("continualTypoCost", 1.0f);
        typoTransformer.setContinualTypoCost(continualTypoCost);

        instances.emplace(id, builder.build(typoTransformer, typoCostThreshold));
    }

    return id;
}

json kiwiReady(Kiwi& kiwi, const json& args) {
    return kiwi.ready();
}

json kiwiIsTypoTolerant(Kiwi& kiwi, const json& args) {
    return kiwi.isTypoTolerant();
}

json kiwiAnalyze(Kiwi& kiwi, const json& args) {
    const std::string str = args[0];
    const Match matchOptions = getAtOrDefault(args, 1, Match::allWithNormalizing);
    const BlockListArg blockListArg(kiwi, args, 2);
    const auto pretokenized = parsePretokenizedArg(args, 3);
    const auto allowedDialects = parseDialects(getAtOrDefault(args, 4, std::string{ "standard" }));
    const auto dialectCost = getAtOrDefault(args, 5, 3.0f);
    
    const TokenResult tokenResult = kiwi.analyze(str, AnalyzeOption{ matchOptions, blockListArg.setPtr(), false, allowedDialects, dialectCost }, pretokenized);

    return serializeTokenResult(kiwi, tokenResult);
}

json kiwiAnalyzeTopN(Kiwi& kiwi, const json& args) {
    const std::string str = args[0];
    const int topN = args[1];
    const Match matchOptions = getAtOrDefault(args, 2, Match::allWithNormalizing);
    const BlockListArg blockListArg(kiwi, args, 3);
    const auto pretokenized = parsePretokenizedArg(args, 4);
    const auto allowedDialects = parseDialects(getAtOrDefault(args, 4, std::string{ "standard" }));
    const auto dialectCost = getAtOrDefault(args, 5, 3.0f);

    const std::vector<TokenResult> tokenResults = kiwi.analyze(str, topN, AnalyzeOption{ matchOptions, blockListArg.setPtr(), false, allowedDialects, dialectCost }, pretokenized);

    return serializeTokenResultVec(kiwi, tokenResults);
}

json kiwiTokenize(Kiwi& kiwi, const json& args) {
    const std::string str = args[0];
    const Match matchOptions = getAtOrDefault(args, 1, Match::allWithNormalizing);
    const BlockListArg blockListArg(kiwi, args, 2);
    const auto pretokenized = parsePretokenizedArg(args, 3);
    const auto allowedDialects = parseDialects(getAtOrDefault(args, 4, std::string{ "standard" }));
    const auto dialectCost = getAtOrDefault(args, 5, 3.0f);
    
    const TokenResult tokenResult = kiwi.analyze(str, AnalyzeOption{ matchOptions, blockListArg.setPtr(), false, allowedDialects, dialectCost }, pretokenized);

    return serializeTokenInfoVec(kiwi, tokenResult.first);
}

json kiwiTokenizeTopN(Kiwi& kiwi, const json& args) {
    const std::string str = args[0];
    const int topN = args[1];
    const Match matchOptions = getAtOrDefault(args, 2, Match::allWithNormalizing);
    const BlockListArg blockListArg(kiwi, args, 3);
    const auto pretokenized = parsePretokenizedArg(args, 4);
    const auto allowedDialects = parseDialects(getAtOrDefault(args, 5, std::string{ "standard" }));
    const auto dialectCost = getAtOrDefault(args, 6, 3.0f);

    const std::vector<TokenResult> tokenResults = kiwi.analyze(str, topN, AnalyzeOption{ matchOptions, blockListArg.setPtr(), false, allowedDialects, dialectCost }, pretokenized);

    json result = json::array();
    for (const TokenResult& tokenResult : tokenResults) {
        result.push_back(serializeTokenInfoVec(kiwi, tokenResult.first));
    }

    return result;
}

json kiwiSplitIntoSents(Kiwi& kiwi, const json& args) {
    const std::string str = args[0];
    const Match matchOptions = getAtOrDefault(args, 1, Match::allWithNormalizing);
    const bool withTokenResult = getAtOrDefault(args, 2, false);

    TokenResult tokenResult;
    const auto sentenceSpans = kiwi.splitIntoSents(str, matchOptions, withTokenResult ? &tokenResult : nullptr);

    json spans = json::array();
    for (const auto& span : sentenceSpans) {
        spans.push_back({
            { "start", span.first },
            { "end", span.second },
        });
    }
    
    return {
        { "spans", spans },
        { "tokenResult", withTokenResult ? serializeTokenResult(kiwi, tokenResult) : nullptr },
    };
}

json kiwiJoinSent(Kiwi& kiwi, const json& args) {
    const json morphs = args[0];
    const bool lmSearch = getAtOrDefault(args, 1, true);
    const bool withRanges = getAtOrDefault(args, 2, false);

    auto joiner = kiwi.newJoiner(lmSearch);

    for (const auto& morph : morphs) {
        const std::string form8 = morph["form"];
        const std::u16string form = utf8To16(form8);

        const std::string tagStr8 = morph["tag"];
        const std::u16string tagStr = utf8To16(tagStr8);
        const POSTag tag = toPOSTag(tagStr);

        const cmb::Space space = morph.value("space", cmb::Space::none);

        joiner.add(form, tag, true, space);
    }

    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    const std::string str = joiner.getU8(withRanges ? &ranges : nullptr);

    json rangesRet = json::array();
    for (const auto& range : ranges) {
        rangesRet.push_back({
            { "start", range.first },
            { "end", range.second },
        });
    }

    return {
        { "str", str },
        { "ranges", withRanges ? rangesRet : nullptr },
    };
}

json kiwiGetGlobalConfig(Kiwi& kiwi, const json& args) {
    json obj = json::object();
    auto config = kiwi.getGlobalConfig();
    obj["integrateAllomorph"] = config.integrateAllomorph;
    obj["cutOffThreshold"] = config.cutOffThreshold;
    obj["unkFormScoreScale"] = config.unkFormScoreScale;
    obj["unkFormScoreBias"] = config.unkFormScoreBias;
    obj["spacePenalty"] = config.spacePenalty;
    obj["typoCostWeight"] = config.typoCostWeight;
    obj["maxUnkFormSize"] = config.maxUnkFormSize;
    obj["spaceTolerance"] = config.spaceTolerance;
    return obj;
}

json kiwiSetGlobalConfig(Kiwi& kiwi, const json& args) {
    KiwiConfig config;
    if (args.contains("integrateAllomorph")) config.integrateAllomorph = args["integrateAllomorph"];
    if (args.contains("cutOffThreshold")) config.cutOffThreshold = args["cutOffThreshold"];
    if (args.contains("unkFormScoreScale")) config.unkFormScoreScale = args["unkFormScoreScale"];
    if (args.contains("unkFormScoreBias")) config.unkFormScoreBias = args["unkFormScoreBias"];
    if (args.contains("spacePenalty")) config.spacePenalty = args["spacePenalty"];
    if (args.contains("typoCostWeight")) config.typoCostWeight = args["typoCostWeight"];
    if (args.contains("maxUnkFormSize")) config.maxUnkFormSize = args["maxUnkFormSize"];
    if (args.contains("spaceTolerance")) config.spaceTolerance = args["spaceTolerance"];
    kiwi.setGlobalConfig(config);
    return nullptr;
}

json kiwiCreateMorphemeSet(Kiwi& kiwi, const json& args) {
    const int id = nextMorphemeSetId();

    const json morphs = args[0];
    std::unordered_set<const Morpheme*> set = parseMorphemeSet(kiwi, morphs);
    
    morphemeSets.emplace(id, set);

    return id;
}

json kiwiDestroyMorphemeSet(Kiwi& kiwi, const json& args) {
    const int id = args[0];
    morphemeSets.erase(id);
    return nullptr;
}


using ApiMethod = json(*)(const json&);
using InstanceApiMethod = json(*)(Kiwi&, const json&);

std::map<std::string, ApiMethod> apiMethods = {
    { "version", version },
    { "build", build },
};

std::map<std::string, InstanceApiMethod> instanceApiMethods = {
    { "ready", kiwiReady },
    { "isTypoTolerant", kiwiIsTypoTolerant },
    { "analyze", kiwiAnalyze },
    { "analyzeTopN", kiwiAnalyzeTopN },
    { "tokenize", kiwiTokenize },
    { "tokenizeTopN", kiwiTokenizeTopN},
    { "splitIntoSents", kiwiSplitIntoSents },
    { "joinSent", kiwiJoinSent },
    { "getGlobalConfig", kiwiGetGlobalConfig },
    { "setGlobalConfig", kiwiSetGlobalConfig },
    { "createMorphemeSet", kiwiCreateMorphemeSet },
    { "destroyMorphemeSet", kiwiDestroyMorphemeSet },
};


std::string api(std::string dataStr) {
    const json data = json::parse(dataStr);

    const std::string methodName = data["method"];
    const json args = data["args"];
    const json id = data.value("id", json(nullptr));

    if (id.is_number_integer()) {
        const int instanceId = id;
        auto& instance = instances[instanceId];
        const auto method = instanceApiMethods[methodName];
        return method(instance, args).dump();
    }

    return apiMethods[methodName](args).dump();
}


EMSCRIPTEN_BINDINGS(kiwi) {
    emscripten::constant("VERSION_MAJOR", KIWI_VERSION_MAJOR);
    emscripten::constant("VERSION_MINOR", KIWI_VERSION_MINOR);
    emscripten::constant("VERSION_PATCH", KIWI_VERSION_PATCH);
    emscripten::constant("VERSION", emscripten::val(KIWI_VERSION_STRING));

    emscripten::function("api", &api);
}
