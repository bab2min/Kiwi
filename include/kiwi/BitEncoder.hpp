#pragma once

#include <vector>
#include <array>
#include "BitUtils.h"
#include "TemplateUtils.hpp"

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
					i |= buf[p] << (shiftBias + (p - packetBegin) * packetBits);
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

		template<ptrdiff_t ...s>
		using BitSeq = tp::seq<s...>;

		namespace detail
		{
			template<class Encoder, size_t offset, size_t depth, ptrdiff_t ...bitSeq>
			struct VLTransform;

			template<class Encoder, size_t offset, size_t depth, ptrdiff_t firstBits, ptrdiff_t ...restBits>
			struct VLTransform<Encoder, offset, depth, firstBits, restBits...>
			{
				Encoder& encoder;

				VLTransform(Encoder& _encoder) : encoder( _encoder )
				{
				}

				void encode(size_t i)
				{
					constexpr size_t z = offset + (1 << firstBits);
					if (i < z)
					{
						return encoder.template write<depth + 1 + firstBits>(((i - offset) << (depth + 1)) | ((1 << depth) - 1));
					}
					return VLTransform<Encoder, z, depth + 1, restBits...>{ encoder }.encode(i);
				}

				static constexpr size_t bias = VLTransform<Encoder, offset + (1 << firstBits), depth + 1, restBits...>::bias;
			};

			template<class Encoder, size_t offset, size_t depth, ptrdiff_t firstBits>
			struct VLTransform<Encoder, offset, depth, firstBits>
			{
				Encoder& encoder;

				VLTransform(Encoder& _encoder) : encoder( _encoder )
				{
				}

				void encode(size_t i)
				{
					constexpr size_t z = offset + (1 << firstBits);
					if (i < z)
					{
						return encoder.template write<depth + firstBits>(((i - offset) << depth) | ((1 << depth) - 1));
					}
					throw std::runtime_error{ "failed to encode. out of range" };
				}

				static constexpr size_t bias = offset + (1 << firstBits);
			};

			template<class Encoder, size_t offset, size_t depth>
			struct VLTransform<Encoder, offset, depth>
			{
				Encoder& encoder;

				VLTransform(Encoder& _encoder) : encoder{ _encoder }
				{
				}

				static constexpr size_t bias = 0;
			};

			template<class Encoder, ptrdiff_t ...bitSeq>
			VLTransform<Encoder, 0, 0, bitSeq...> makeVLTransform(Encoder& enc, BitSeq<bitSeq...>)
			{
				return { enc };
			}

			inline size_t getPrefixWidth(uint32_t mask)
			{
				return utils::countTrailingZeroes(~mask);
			}

			inline size_t getPrefixWidth(uint64_t mask)
			{
				return utils::countTrailingZeroes(~mask);
			}
#ifdef __APPLE__
			inline size_t getPrefixWidth(size_t mask) { return getPrefixWidth((uint64_t)mask); }
#endif
		}

		template<class Stream, class BitSeqs, class Packet = uint8_t, size_t bufSize = 64>
		class VariableLengthEncoder
		{
			template<class Encoder, size_t offset, size_t depth, ptrdiff_t ...bitSeq>
			friend struct detail::VLTransform;
		
		protected:
			static constexpr size_t packetBits = sizeof(Packet) * 8;
			std::array<Packet, bufSize> buf = { {0,} };
			Packet lastPacket = 0;
			ptrdiff_t bitPos = 0;
			Stream stream;

			void fetch()
			{
				lastPacket = buf[bufSize - 1];
				stream.read((char*)buf.data(), bufSize * sizeof(Packet));
			}

			template<size_t bitwidth>
			void write(size_t i)
			{
				const ptrdiff_t packetPrefix = bitPos / packetBits;
				const ptrdiff_t bitP = bitPos % packetBits;
				const ptrdiff_t packetBegin = (bitPos + packetBits - 1) / packetBits;
				const ptrdiff_t packetLen = (bitPos + bitwidth + packetBits - 1) / packetBits - packetBegin;
				
				if (bitP)
				{
					buf[packetPrefix] |= static_cast<Packet>(i << bitP);
					i >>= packetBits - bitP;
				}

				size_t p, pp;
				for (p = 0, pp = packetBegin; p < packetLen; ++p, ++pp)
				{
					if (pp == bufSize)
					{
						flush(true);
						pp = 0;
					}
					buf[pp] = static_cast<Packet>(i);
					i >>= packetBits;
				}
				bitPos = (bitPos + bitwidth) % (bufSize * packetBits);
				if (bitPos == 0 && pp == bufSize)
				{
					flush(true);
				}
			}

			size_t readBits(size_t width)
			{
				size_t i = 0;

				ptrdiff_t packetPrefix;
				ptrdiff_t bitP;
				ptrdiff_t packetBegin;
				ptrdiff_t packetLen;
				ptrdiff_t shiftBias;
				if (bitPos < 0)
				{
					i = lastPacket >> (bitPos + packetBits);
					packetPrefix = 0;
					bitP = 0;
					packetBegin = 0;
					packetLen = (bitPos + width + packetBits - 1) / packetBits - packetBegin;
					shiftBias = -bitPos;
				}
				else
				{
					packetPrefix = bitPos / packetBits;
					bitP = bitPos % packetBits;
					packetBegin = (bitPos + packetBits - 1) / packetBits;
					packetLen = (bitPos + width + packetBits - 1) / packetBits - packetBegin;
					shiftBias = bitP ? (packetBits - bitP) : 0;
				}
				
				if (bitP)
				{
					i = buf[packetPrefix] >> bitP;
				}

				size_t p, pp;
				for (p = 0, pp = packetBegin; p < packetLen; ++p, ++pp)
				{
					if (pp == bufSize)
					{
						fetch();
						pp = 0;
					}
					i |= buf[pp] << (shiftBias + p * packetBits);
				}
				if (bitPos > 0 && (bitPos + width) % (bufSize * packetBits) == 0 && pp == bufSize)
				{
					fetch();
				}
				
				if (bitPos >= 0) bitPos = (bitPos + width) % (bufSize * packetBits);
				else bitPos += width;
				return i & ((1 << width) - 1);
			}

			template<ptrdiff_t widthIdx>
			size_t readV()
			{
				size_t i = readBits(tp::get<widthIdx, BitSeqs>::value);
				return i + decltype(detail::makeVLTransform<VariableLengthEncoder>(*this, tp::Slice<widthIdx, BitSeqs>{}))::bias;
			}

			template<ptrdiff_t ...indices>
			size_t readVDispatch(size_t width, tp::seq<indices...>)
			{
				using ReadFn = size_t(VariableLengthEncoder::*)();

				static constexpr ReadFn table[] = {
					&VariableLengthEncoder::readV<indices>...
				};
				return (this->*table[width])();
			}

		public:

			static constexpr size_t min_value = 0;
			static constexpr size_t max_value = decltype(detail::makeVLTransform<VariableLengthEncoder>(std::declval<VariableLengthEncoder&>(), BitSeqs{}))::bias - 1;

			template<class ...Args>
			VariableLengthEncoder(Args&&... args)
				: stream( std::forward<Args>(args)... )
			{
			}

			void write(size_t i)
			{
				detail::makeVLTransform<VariableLengthEncoder>(*this, BitSeqs{}).encode(i);
			}

			size_t read()
			{
				constexpr size_t maxPrefixWidth = tp::SeqSize<BitSeqs>::value - 1;
				size_t i = readBits(maxPrefixWidth);
				size_t prefixWidth = detail::getPrefixWidth(i);
				bitPos -= maxPrefixWidth - std::min(prefixWidth + 1, maxPrefixWidth);
				return readVDispatch(prefixWidth, tp::gen_seq<tp::SeqSize<BitSeqs>::value>{});
			}

			void flush(bool full = false)
			{
				stream.write((const char*)buf.data(), full ? (bufSize * sizeof(Packet)) : ((bitPos + packetBits - 1) / packetBits * sizeof(Packet)));
				std::fill(buf.begin(), buf.end(), 0);
			}

			Stream& getStream() { return stream; }
			const Stream& getStream() const { return stream; }
		};

		template<class Stream, class BitSeqs, class Packet = uint8_t, size_t bufSize = 64>
		class VariableLengthDecoder : public VariableLengthEncoder<Stream, BitSeqs, Packet, bufSize>
		{
		public:
			template<class ...Args>
			VariableLengthDecoder(Args&&... args)
				: VariableLengthEncoder<Stream, BitSeqs, Packet, bufSize>( std::forward<Args>(args)... )
			{
				this->fetch();
			}
		};
	}
}
