#include <kiwi/Kiwi.h>

#include <emscripten.h>
#include <emscripten/val.h>
#include <emscripten/bind.h>

using namespace kiwi;


static Kiwi* kiwiInstance = nullptr;

void destroy() {
    if (kiwiInstance) {
        delete kiwiInstance;
        kiwiInstance = nullptr;
    }
}

void init(const std::string modelPath) {
    destroy();

    KiwiBuilder builder = KiwiBuilder{ modelPath, 0, BuildOption::default_, false };
    kiwiInstance = new Kiwi(builder.build(DefaultTypoSet::withoutTypo));
}

std::vector<TokenInfo> analyze(const std::u16string text) {
    if (!kiwiInstance) {
        throw std::runtime_error("Not initialized");
    }

    const auto tokenResults = kiwiInstance->analyze(text, 1, Match::allWithNormalizing);
    const auto tokenResult = tokenResults[0];

    return tokenResult.first;
}

emscripten::val tokenInfoTagToString(const TokenInfo& tokenInfo) {
    return emscripten::val(tagToString(tokenInfo.tag));
}


EMSCRIPTEN_BINDINGS(kiwi) {
    emscripten::constant("VERSION_MAJOR", KIWI_VERSION_MAJOR);
    emscripten::constant("VERSION_MINOR", KIWI_VERSION_MINOR);
    emscripten::constant("VERSION_PATCH", KIWI_VERSION_PATCH);
    emscripten::constant("VERSION", emscripten::val(KIWI_VERSION_STRING));

    emscripten::function("init", &init);
    emscripten::function("destroy", &destroy);
    emscripten::function("analyze", &analyze);

    emscripten::class_<TokenInfo>("TokenInfo")
        .property("str", &TokenInfo::str)
        .property("position", &TokenInfo::position)
        .property("length", &TokenInfo::length)
        .function("tagToString", &tokenInfoTagToString);

    emscripten::register_vector<TokenInfo>("vector<TokenInfo>");
}
