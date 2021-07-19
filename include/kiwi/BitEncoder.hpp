#pragma once

#include <vector>
#include <array>
#include "BitUtils.h"

namespace kiwi
{
	namespace lm
	{
		namespace detail
		{
			template<size_t a, size_t b> 
			struct gcd
			{
				static constexpr size_t value = gcd<b, a% b>::value;
			};

			template<size_t a>
			struct gcd<a, 0>
			{
				static constexpr size_t value = a;
			};

			template<size_t a, size_t b>
			struct lcm
			{
				static constexpr size_t value = a * b / gcd<a, b>::value;
			};

			template<class _T> using Invoke = typename _T::type;

			template<ptrdiff_t...> struct seq { using type = seq; };

			template<class _S1, class _S2> struct concat;

			template<ptrdiff_t... _i1, ptrdiff_t... _i2>
			struct concat<seq<_i1...>, seq<_i2...>>
				: seq<_i1..., (sizeof...(_i1) + _i2)...> {};

			template<class _S1, class _S2>
			using Concat = Invoke<concat<_S1, _S2>>;

			template<size_t _n> struct gen_seq;
			template<size_t _n> using GenSeq = Invoke<gen_seq<_n>>;

			template<size_t _n>
			struct gen_seq : Concat<GenSeq<_n / 2>, GenSeq<_n - _n / 2>> {};

			template<> struct gen_seq<0> : seq<> {};
			template<> struct gen_seq<1> : seq<0> {};

			template<class Ty>
			struct SeqSize;

			template<ptrdiff_t ..._i>
			struct SeqSize<seq<_i...>>
			{
				static constexpr size_t value = sizeof...(_i);
			};

			template<size_t n, class Seq, ptrdiff_t ..._j>
			struct slice;

			template<size_t n, class Seq, ptrdiff_t ..._j>
			using Slice = Invoke<slice<n, Seq, _j...>>;

			template<size_t n, ptrdiff_t first, ptrdiff_t ..._i, ptrdiff_t ..._j>
			struct slice<n, seq<first, _i...>, _j...>
			{
				using type = Slice<n - 1, seq<_i...>, _j..., first>;
			};

			template<ptrdiff_t first, ptrdiff_t ..._i, ptrdiff_t ..._j>
			struct slice<0, seq<first, _i...>, _j...>
			{
				using type = seq<_j...>;
			};

			template<ptrdiff_t ..._j>
			struct slice<0, seq<>, _j...>
			{
				using type = seq<_j...>;
			};

			template<size_t n, class Seq, ptrdiff_t ...j>
			struct get;

			template<size_t n, ptrdiff_t first, ptrdiff_t ..._i>
			struct get<n, seq<first, _i...>> : get<n - 1, seq<_i...>>
			{
			};

			template<ptrdiff_t first, ptrdiff_t ..._i>
			struct get<0, seq<first, _i...>> : std::integral_constant<ptrdiff_t, first>
			{
			};

			template<>
			struct get<0, seq<>>
			{
			};
		}

		template<class Stream, size_t bits, class Packet = uint8_t>
		class FixedLengthEncoder
		{
			static constexpr size_t packetBits = sizeof(Packet) * 8;
			static constexpr size_t bufSize = bits / detail::gcd<bits, packetBits>::value;
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
			void writeDispatch(size_t i, detail::seq<indices...>)
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
			size_t readDispatch(detail::seq<indices...>)
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
				: stream{ std::forward<Args>(args)... }
			{
			}

			void write(size_t i)
			{
				return writeDispatch(i & mask, detail::gen_seq<numPhases>{});
			}

			size_t read()
			{
				return readDispatch(detail::gen_seq<numPhases>{});
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
		using BitSeq = detail::seq<s...>;

		namespace detail
		{
			template<class Encoder, size_t offset, size_t depth, ptrdiff_t ...bitSeq>
			struct VLTransform;

			template<class Encoder, size_t offset, size_t depth, ptrdiff_t firstBits, ptrdiff_t ...restBits>
			struct VLTransform<Encoder, offset, depth, firstBits, restBits...>
			{
				Encoder& encoder;

				VLTransform(Encoder& _encoder) : encoder{ _encoder }
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

				VLTransform(Encoder& _encoder) : encoder{ _encoder }
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
				size_t i = readBits(detail::get<widthIdx, BitSeqs>::value);
				return i + decltype(detail::makeVLTransform<VariableLengthEncoder>(*this, detail::Slice<widthIdx, BitSeqs>{}))::bias;
			}

			template<ptrdiff_t ...indices>
			size_t readVDispatch(size_t width, detail::seq<indices...>)
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
				: stream{ std::forward<Args>(args)... }
			{
			}

			void write(size_t i)
			{
				detail::makeVLTransform<VariableLengthEncoder>(*this, BitSeqs{}).encode(i);
			}

			size_t read()
			{
				constexpr size_t maxPrefixWidth = detail::SeqSize<BitSeqs>::value - 1;
				size_t i = readBits(maxPrefixWidth);
				size_t prefixWidth = detail::getPrefixWidth(i);
				bitPos -= maxPrefixWidth - std::min(prefixWidth + 1, maxPrefixWidth);
				return readVDispatch(prefixWidth, detail::gen_seq<detail::SeqSize<BitSeqs>::value>{});
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
				: VariableLengthEncoder<Stream, BitSeqs, Packet, bufSize>{ std::forward<Args>(args)... }
			{
				this->fetch();
			}
		};
	}
}
