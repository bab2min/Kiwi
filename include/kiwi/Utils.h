#pragma once
#include "Types.h"

namespace kiwi
{
	std::u16string utf8To16(const std::string& str);
	std::string utf16To8(const std::u16string& str);
}