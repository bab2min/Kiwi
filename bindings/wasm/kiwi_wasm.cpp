#include <kiwi/Kiwi.h>

#include <emscripten.h>
#include <emscripten/val.h>
#include <emscripten/bind.h>

EMSCRIPTEN_BINDINGS(kiwi) {
    emscripten::constant("VERSION_MAJOR", KIWI_VERSION_MAJOR);
    emscripten::constant("VERSION_MINOR", KIWI_VERSION_MINOR);
    emscripten::constant("VERSION_PATCH", KIWI_VERSION_PATCH);
    emscripten::constant("VERSION", emscripten::val(KIWI_VERSION_STRING));
}