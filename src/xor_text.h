#pragma once
#include "../xor.h"

namespace jm::detail
{
    // Share the decode routine between strings with the same storage type.
    template <class Text>
    __declspec(noinline) void decode_text(Text& text)
    {
        text.crypt();
    }

    // CRT startup decodes these owners before DllMain starts either worker.
    // Call sites only take a pointer; no per-frame decryption or lazy-init guard.
    template <class Factory>
    inline const auto persistent_text = Factory::make();
} // namespace jm::detail

// Unlike xor_a(), the pointer remains valid after the calling expression.
#define xor_text(str) ([]() { struct Factory { static auto make() { auto text = xorstr(str); ::jm::detail::decode_text(text); return text; } }; return ::jm::detail::persistent_text<Factory>.get(); }())
