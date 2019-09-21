#pragma once
#include "KiwiHeader.h"
#include "serializer.hpp"

using namespace std;
using namespace kiwi;

uint32_t kiwi::serializer::readVFromBinStream(std::istream & is)
{
	static uint32_t vSize[] = { 0, 0x80, 0x4080, 0x204080, 0x10204080 };
	char c;
	uint32_t v = 0;
	size_t i;
	for (i = 0; (c = serializer::readFromBinStream<uint8_t>(is)) & 0x80; ++i)
	{
		v |= (c & 0x7F) << (i * 7);
	}
	v |= c << (i * 7);
	return v + vSize[i];
}

uint32_t kiwi::serializer::readVFromBinStream(serializer::imstream& is)
{
	static uint32_t vSize[] = { 0, 0x80, 0x4080, 0x204080, 0x10204080 };
	char c;
	uint32_t v = 0;
	size_t i;
	for (i = 0; (c = is.get()[i]) & 0x80; ++i)
	{
		v |= (c & 0x7F) << (i * 7);
	}
	v |= c << (i * 7);
	is.seek(i + 1);
	return v + vSize[i];
}

void kiwi::serializer::writeVToBinStream(std::ostream & os, uint32_t v)
{
	static uint32_t vSize[] = { 0, 0x80, 0x4080, 0x204080, 0x10204080 };
	size_t i;
	for (i = 1; i <= 4; ++i)
	{
		if (v < vSize[i]) break;
	}
	v -= vSize[i - 1];
	for (size_t n = 0; n < i; ++n)
	{
		uint8_t c = (v & 0x7F) | (n + 1 < i ? 0x80 : 0);
		serializer::writeToBinStream(os, c);
		v >>= 7;
	}
}


int32_t kiwi::serializer::readSVFromBinStream(std::istream & is)
{
	static int32_t vSize[] = { 0x40, 0x2000, 0x100000, 0x8000000 };
	char c;
	uint32_t v = 0;
	size_t i;
	for (i = 0; (c = serializer::readFromBinStream<uint8_t>(is)) & 0x80; ++i)
	{
		v |= (c & 0x7F) << (i * 7);
	}
	v |= c << (i * 7);
	if (i >= 4) return (int32_t)v;
	return v - (v >= vSize[i] ? (1 << ((i + 1) * 7)) : 0);
}

int32_t kiwi::serializer::readSVFromBinStream(serializer::imstream& is)
{
	static int32_t vSize[] = { 0x40, 0x2000, 0x100000, 0x8000000 };
	char c;
	uint32_t v = 0;
	size_t i;
	for (i = 0; (c = is.get()[i]) & 0x80; ++i)
	{
		v |= (c & 0x7F) << (i * 7);
	}
	v |= c << (i * 7);
	is.seek(i + 1);
	if (i >= 4) return (int32_t)v;
	return v - (v >= vSize[i] ? (1 << ((i + 1) * 7)) : 0);
}

void kiwi::serializer::writeSVToBinStream(std::ostream & os, int32_t v)
{
	static int32_t vSize[] = { 0, 0x40, 0x2000, 0x100000, 0x8000000 };
	size_t i;
	for (i = 1; i <= 4; ++i)
	{
		if (-vSize[i] <= v && v < vSize[i]) break;
	}
	uint32_t u;
	if (i >= 5) u = (uint32_t)v;
	else u = v + (v < 0 ? (1 << (i * 7)) : 0);
	for (size_t n = 0; n < i; ++n)
	{
		uint8_t c = (u & 0x7F) | (n + 1 < i ? 0x80 : 0);
		serializer::writeToBinStream(os, c);
		u >>= 7;
	}
}

void kiwi::serializer::writeNegFixed16(std::ostream& os, float v)
{
	assert(v <= 0);
	auto dv = (uint16_t)min(-v * (1 << 12), 65535.f);
	serializer::writeToBinStream(os, dv);
}

float kiwi::serializer::readNegFixed16(std::istream& is)
{
	auto dv = serializer::readFromBinStream<uint16_t>(is);
	return -(dv / float(1 << 12));
}

float kiwi::serializer::readNegFixed16(imstream& is)
{
	auto dv = serializer::readFromBinStream<uint16_t>(is);
	return -(dv / float(1 << 12));
}
