#pragma once

#include <vector>
#include <array>
#include <kiwi/BitUtils.h>
#include <kiwi/TemplateUtils.hpp>

namespace kiwi
{
	namespace lm
	{
		template<class Stream, size_t bits, class Packet = uint8_t>
		class FixedLengthEncoder
		{
			static constexpr size_t packetBits = sizeof(Packet) * 8;
			static constexpr size_t bufSize = bits / tp::gcd<bits, packetBits>::value;
			static constexpr size_t numPhases = bufSize * packetBits / bits;
			static constexpr size_t mask = (1 << bits) - 1;
			std::array<Packet, bufSize> buf = { {0,} };
			size_t bPhase = 0;
			Stream stream;

			void fetch()
			{
				stream.read((char*)buf.data(), bufSize * sizeof(Packet));
			}

			template<ptrdiff_t phase>
			void writePhase(size_t i)
			{
				constexpr size_t packetPrefix = (bits * phase) / packetBits;
				constexpr size_t bitPos = (bits * phase) % packetBits;
				constexpr size_t packetBegin = (bits * phase + packetBits - 1) / packetBits;
				constexpr size_t packetEnd = (bits * (phase + 1) + packetBits - 1) / packetBits;
				
				if (bitPos)
				{
					buf[packetPrefix] |= static_cast<Packet>(i << bitPos);
					i >>= packetBits - bitPos;
				}

				for (size_t p = packetBegin; p < packetEnd; ++p)
				{
					buf[p] = static_cast<Packet>(i);
					i >>= packetBits;
				}

				bPhase++;
				if (phase == numPhases - 1)
				{
					flush();
				}
			}

			template<ptrdiff_t ...indices>
			void writeDispatch(size_t i, tp::seq<indices...>)
			{
				using WriteFn = void(FixedLengthEncoder::*)(size_t);

				static constexpr WriteFn table[] = {
					&FixedLengthEncoder::writePhase<indices>...
				};
				return (this->*table[bPhase])(i);
			}

			template<ptrdiff_t phase>
			size_t readPhase()
			{
				constexpr size_t packetPrefix = (bits * phase) / packetBits;
				constexpr size_t bitPos = (bits * phase) % packetBits;
				constexpr size_t packetBegin = (bits * phase + packetBits - 1) / packetBits;
				constexpr size_t packetEnd = (bits * (phase + 1) + packetBits - 1) / packetBits;
				constexpr size_t shiftBias = bitPos ? (packetBits - bitPos) : 0;
				
				if (phase == 0)
				{
					fetch();
				}

				size_t i = 0;
				if (bitPos)
				{
					i = buf[packetPrefix] >> bitPos;
				}

				for (size_t p = packetBegin; p < packetEnd; ++p)
				{
					i |= (size_t)buf[p] << (shiftBias + (p - packetBegin) * packetBits);
				}

				if (phase == numPhases - 1)
				{
					bPhase = 0;
				}
				else
				{
					bPhase++;
				}
				return i & mask;
			}

			template<ptrdiff_t ...indices>
			size_t readDispatch(tp::seq<indices...>)
			{
				using ReadFn = size_t(FixedLengthEncoder::*)();

				static constexpr ReadFn table[] = {
					&FixedLengthEncoder::readPhase<indices>...
				};
				return (this->*table[bPhase])();
			}

		public:

			template<class ...Args>
			FixedLengthEncoder(Args&&... args)
				: stream( std::forward<Args>(args)... )
			{
			}

			void write(size_t i)
			{
				return writeDispatch(i & mask, tp::gen_seq<numPhases>{});
			}

			size_t read()
			{
				return readDispatch(tp::gen_seq<numPhases>{});
			}

			void flush()
			{
				stream.write((const char*)buf.data(), ((bPhase * bits + packetBits - 1) / packetBits) * sizeof(Packet));
				std::fill(buf.begin(), buf.end(), 0);
				bPhase = 0;
			}

			Stream& getStream() { return stream; }
			const Stream& getStream() const { return stream; }
		};
	}
}
