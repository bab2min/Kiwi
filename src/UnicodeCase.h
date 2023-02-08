#pragma once
#include <string>
#include "StrUtils.h"

namespace kiwi
{
    char32_t toLower(char32_t c);
    char32_t toUpper(char32_t c);

    int toLower(char32_t c, char32_t* out);
    int toUpper(char32_t c, char32_t* out);

    template<class CharIt>
    CharIt decodeUtf8(CharIt s, char32_t& out)
    {
        if ((*s & 0x80) == 0)
        {
            out = *s++;
        }
        else if ((*s & 0xE0) == 0xC0)
        {
            out = (*s++ & 0x1F) << 6;
            out |= (*s++ & 0x3F);
        }
        else if ((*s & 0xF0) == 0xE0)
        {
            out = (*s++ & 0x0F) << 12;
            out |= (*s++ & 0x3F) << 6;
            out |= (*s++ & 0x3F);
        }
        else
        {
            out = (*s++ & 0x07) << 18;
            out |= (*s++ & 0x3F) << 12;
            out |= (*s++ & 0x3F) << 6;
            out |= (*s++ & 0x3F);
        }
        return s;
    }

    template<class CharIt>
    CharIt encodeUtf8(char32_t c, CharIt out)
    {
        if (c < 0x80)
        {
            *out++ = c;
        }
        else if (c < 0x800)
        {
            *out++ = 0xC0 | (c >> 6);
            *out++ = 0x80 | (c & 0x3F);
        }
        else if (c < 0x10000)
        {
            *out++ = 0xE0 | (c >> 12);
            *out++ = 0x80 | ((c >> 6) & 0x3F);
            *out++ = 0x80 | (c & 0x3F);
        }
        else
        {
            *out++ = 0xF0 | (c >> 18);
            *out++ = 0x80 | ((c >> 12) & 0x3F);
            *out++ = 0x80 | ((c >> 6) & 0x3F);
            *out++ = 0x80 | (c & 0x3F);
        }
        return out;
    }

    template<class CharIt>
    CharIt decodeUtf16(CharIt s, char32_t& out)
    {
        if (isHighSurrogate(*s))
        {
            char16_t a = *s++;
            char16_t b = *s++;
            out = mergeSurrogate(a, b);
        }
        else
        {
            out = *s++;
        }
        return s;
    }

    template<class CharIt>
    CharIt encodeUtf16(char32_t c, CharIt out)
    {
        if (c < 0x10000)
        {
            *out++ = c;
        }
        else
        {
            auto c16 = decomposeSurrogate(c);
            *out++ = c16[0];
            *out++ = c16[1];
        }
        return out;
    }

    template<class CharIt, class OutIt>
    OutIt toLower(CharIt first, CharIt last, OutIt out)
    {
        int size;
        char32_t c;
        char32_t buf[4];
        while (first != last)
        {
            first = decodeUtf8(first, c);
            size = toLower(c, buf);
            for (int i = 0; i < size; ++i)
            {
                out = encodeUtf8(buf[i], out);
            }
        }
        return out;
    }

    template<class CharIt, class OutIt>
    OutIt toUpper(CharIt first, CharIt last, OutIt out)
    {
        int size;
        char32_t c;
        char32_t buf[4];
        while (first != last)
        {
            first = decodeUtf8(first, c);
            size = toUpper(c, buf);
            for (int i = 0; i < size; ++i)
            {
                out = encodeUtf8(buf[i], out);
            }
        }
        return out;
    }

    template<class CharIt, class OutIt>
    OutIt toLower16(CharIt first, CharIt last, OutIt out)
    {
        int size;
        char32_t c;
        char32_t buf[4];
        while (first != last)
        {
            first = decodeUtf16(first, c);
            size = toLower(c, buf);
            for (int i = 0; i < size; ++i)
            {
                out = encodeUtf16(buf[i], out);
            }
        }
        return out;
    }

    template<class CharIt, class OutIt>
    OutIt toUpper16(CharIt first, CharIt last, OutIt out)
    {
        int size;
        char32_t c;
        char32_t buf[4];
        while (first != last)
        {
            first = decodeUtf16(first, c);
            size = toUpper(c, buf);
            for (int i = 0; i < size; ++i)
            {
                out = encodeUtf16(buf[i], out);
            }
        }
        return out;
    }

    inline std::string toLower(const std::string& s)
    {
        std::string ret;
        ret.reserve(s.size());
        toLower(s.begin(), s.end(), std::back_inserter(ret));
        return ret;
    }

    inline std::string toUpper(const std::string& s)
    {
        std::string ret;
        ret.reserve(s.size());
        toUpper(s.begin(), s.end(), std::back_inserter(ret));
        return ret;
    }


    inline std::u16string toLower(const std::u16string& s)
    {
        std::u16string ret;
        ret.reserve(s.size());
        toLower16(s.begin(), s.end(), std::back_inserter(ret));
        return ret;
    }

    inline std::u16string toUpper(const std::u16string& s)
    {
        std::u16string ret;
        ret.reserve(s.size());
        toUpper16(s.begin(), s.end(), std::back_inserter(ret));
        return ret;
    }
}
