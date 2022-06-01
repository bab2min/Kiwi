#pragma once
#include <vector>
#include <array>
#include <kiwi/BitUtils.h>
#include <kiwi/ArchUtils.h>
#include <kiwi/TemplateUtils.hpp>

namespace kiwi
{
	namespace qe
	{
		template<uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4>
		class QCode
		{
			static constexpr size_t bEnd1 = 1 << b1;
			static constexpr size_t bEnd2 = bEnd1 + (1 << b2);
			static constexpr size_t bEnd3 = bEnd2 + (1 << b3);
			static constexpr size_t qBits[4] = {
				b1, b2, b3, b4
			};
			static constexpr size_t qBias[4] = {
				0, bEnd1, bEnd2, bEnd3,
			};

		public:

			using SrcTy = uint16_t;
			static constexpr size_t largestBitSize = b4;

			template<size_t pack, class IntTy>
			static std::pair<IntTy*, uint8_t> encodePack(uint8_t* header, IntTy* body, uint8_t b, const uint16_t* ints);

			template<size_t pack, class IntTy>
			static std::pair<const IntTy*, uint8_t> decodePack(uint16_t* ints, const uint8_t* header, const IntTy* body, uint8_t b);

			template<class IntTy>
			static std::pair<IntTy*, uint8_t> encodeRest(uint8_t* header, IntTy* body, uint8_t b, const uint16_t* ints, size_t size);

			template<class IntTy>
			static std::pair<const IntTy*, uint8_t> decodeRest(uint16_t* ints, const uint8_t* header, const IntTy* body, uint8_t b, size_t size);
			
			template<size_t pack, class IntTy>
			static std::pair<IntTy*, uint8_t> encode(uint8_t* header, IntTy* body, uint8_t b, const uint16_t* ints, size_t size);

			template<size_t pack, class IntTy>
			static std::pair<const IntTy*, uint8_t> decode(uint16_t* ints, const uint8_t* header, const IntTy* body, uint8_t b, size_t size);
		};

		template<class Code>
		class Encoder
		{
			std::vector<uint8_t> header, body;
			size_t cnt = 0, bitOffset = 0, wordOffset = 0;
		public:

			const std::vector<uint8_t>& getHeader() const { return header; }
			const size_t headerSize() const { return (cnt + 3) / 4; }
			const std::vector<uint8_t>& getBody() const { return body; }
			const size_t bodySize() const { return (wordOffset + (bitOffset ? 1 : 0)) * sizeof(size_t); }

			template<size_t pack, class It>
			void encode(It first, It last)
			{
				typename Code::SrcTy items[pack];
				while(first != last)
				{
					size_t i;
					for (i = 0; i < pack && first != last; ++i)
					{
						items[i] = *first;
						++first;
					}
					header.resize(header.size() + (i + 3) / 4);
					body.resize(body.size() + (Code::largestBitSize * i + 7) / 8);
					if (i >= pack)
					{
						auto p = Code::template encodePack<pack>(&header[cnt / 4], (size_t*)body.data() + wordOffset, bitOffset, items);
						wordOffset = p.first - (size_t*)body.data();
						bitOffset = p.second;
					}
					else
					{
						auto p = Code::encodeRest(&header[cnt / 4], (size_t*)body.data() + wordOffset, bitOffset, items, i);
						wordOffset = p.first - (size_t*)body.data();
						bitOffset = p.second;
					}
					cnt += i;
				}
			}
		};


		template<uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4>
		template<size_t pack, class IntTy>
		std::pair<IntTy*, uint8_t> QCode<b1, b2, b3, b4>::encodePack(uint8_t* header, IntTy* body, uint8_t b, const uint16_t* ints)
		{
			static constexpr size_t bitSize = sizeof(IntTy) * 8;
			uint8_t q[pack] = { 0, };
			for (size_t i = 0; i < pack; ++i)
			{
				q[i] += (ints[i] >= qBias[1]) ? 1 : 0;
				q[i] += (ints[i] >= qBias[2]) ? 1 : 0;
				q[i] += (ints[i] >= qBias[3]) ? 1 : 0;
			}

			for (size_t i = 0; i < pack; i += 4)
			{
				header[i / 4] = q[i] | (q[i + 1] << 2) | (q[i + 2] << 4) | (q[i + 3] << 6);
			}

			size_t u = 0;
			for (size_t i = 0; i < pack; ++i)
			{
				if (!qBits[q[i]]) continue;
				IntTy e = ints[i] - qBias[q[i]];
				if (b + qBits[q[i]] <= bitSize)
				{
					body[u] |= e << (IntTy)b;
				}
				else
				{
					body[u] |= e << (IntTy)b;
					body[u + 1] = e >> (IntTy)(bitSize - b);
				}
				b += qBits[q[i]];
				if (b >= bitSize)
				{
					b -= bitSize;
					u++;
				}
			}
			return std::make_pair(body + u, b);
		}

		template<uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4>
		template<size_t pack, class IntTy>
		std::pair<const IntTy*, uint8_t> QCode<b1, b2, b3, b4>::decodePack(uint16_t* ints, const uint8_t* header, const IntTy* body, uint8_t b)
		{
			static constexpr size_t bitSize = sizeof(IntTy) * 8;
			static constexpr IntTy one = 1;

			uint8_t q[pack] = { 0, };
			for (size_t i = 0; i < pack; i += 4)
			{
				q[i] = header[i / 4] & 3;
				q[i + 1] = (header[i / 4] >> 2) & 3;
				q[i + 2] = (header[i / 4] >> 4) & 3;
				q[i + 3] = (header[i / 4] >> 6) & 3;
			}

			size_t u = 0;
			for (size_t i = 0; i < pack; ++i)
			{
				IntTy e = 0;
				if (qBits[q[i]])
				{
					if (b + qBits[q[i]] <= bitSize)
					{
						e = (body[u] >> (IntTy)b) & ((one << (IntTy)qBits[q[i]]) - 1);
					}
					else
					{
						e = body[u] >> (IntTy)b;
						e |= (body[u + 1] & ((one << (IntTy)(qBits[q[i]] + b - bitSize)) - 1)) << (IntTy)(bitSize - b);
					}
					b += (uint8_t)qBits[q[i]];
					if (b >= bitSize)
					{
						b -= bitSize;
						u++;
					}
				}
				ints[i] = e + (IntTy)qBias[q[i]];
			}
			return std::make_pair(body + u, b);
		}

		template<uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4>
		template<class IntTy>
		inline std::pair<IntTy*, uint8_t> QCode<b1, b2, b3, b4>::encodeRest(uint8_t* header, IntTy* body, uint8_t b, const uint16_t* ints, size_t size)
		{
			static constexpr size_t bitSize = sizeof(IntTy) * 8;

			size_t u = 0;
			for (size_t i = 0; i < size; ++i)
			{
				uint8_t q = 0;
				q += (ints[i] >= qBias[1]) ? 1 : 0;
				q += (ints[i] >= qBias[2]) ? 1 : 0;
				q += (ints[i] >= qBias[3]) ? 1 : 0;

				header[i / 4] |= q << ((i % 4) * 2);

				if (!qBits[q]) continue;
				IntTy e = ints[i] - qBias[q];
				if (b + qBits[q] <= bitSize)
				{
					body[u] |= e << (IntTy)b;
				}
				else
				{
					body[u] |= e << (IntTy)b;
					body[u + 1] = e >> (IntTy)(bitSize - b);
				}
				b += qBits[q];
				if (b >= bitSize)
				{
					b -= bitSize;
					u++;
				}
			}
			return std::make_pair(body + u, b);
		}

		template<uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4>
		template<class IntTy>
		inline std::pair<const IntTy*, uint8_t> QCode<b1, b2, b3, b4>::decodeRest(uint16_t* ints, const uint8_t* header, const IntTy* body, uint8_t b, size_t size)
		{
			static constexpr size_t bitSize = sizeof(IntTy) * 8;
			static constexpr IntTy one = 1;

			size_t u = 0;
			for (size_t i = 0; i < size; ++i)
			{
				uint8_t q = (header[i / 4] >> ((i % 4) * 2)) & 3;
				
				IntTy e = 0;
				if (qBits[q])
				{
					if (b + qBits[q] <= bitSize)
					{
						e = (body[u] >> (IntTy)b) & ((one << (IntTy)qBits[q]) - 1);
					}
					else
					{
						e = body[u] >> (IntTy)b;
						e |= (body[u + 1] & ((one << (IntTy)(qBits[q] + b - bitSize)) - 1)) << (IntTy)(bitSize - b);
					}
					b += (uint8_t)qBits[q];
					if (b >= bitSize)
					{
						b -= bitSize;
						u++;
					}
				}
				ints[i] = e + (IntTy)qBias[q];
			}
			return std::make_pair(body + u, b);
		}

		template<uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4>
		template<size_t pack, class IntTy>
		inline std::pair<IntTy*, uint8_t> QCode<b1, b2, b3, b4>::encode(uint8_t* header, IntTy* body, uint8_t b, const uint16_t* ints, size_t size)
		{
			for (size_t i = 0; i < size / pack; ++i)
			{
				auto p = encodePack<pack>(header, body, b, &ints[i * pack]);
				body = p.first;
				b = p.second;
				header += pack / 4;
			}

			return encodeRest(header, body, b, &ints[size - size % pack], size % pack);
		}

		template<uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4>
		template<size_t pack, class IntTy>
		inline std::pair<const IntTy*, uint8_t> QCode<b1, b2, b3, b4>::decode(uint16_t* ints, const uint8_t* header, const IntTy* body, uint8_t b, size_t size)
		{
			for (size_t i = 0; i < size / pack; ++i)
			{
				auto p = decodePack<pack>(&ints[i * pack], header, body, b);
				body = p.first;
				b = p.second;
				header += pack / 4;
			}

			return decodeRest(&ints[size - size % pack], header, body, b, size % pack);
		}

		template<uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4>
		constexpr size_t QCode<b1, b2, b3, b4>::qBits[4];

		template<uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4>
		constexpr size_t QCode<b1, b2, b3, b4>::qBias[4];
	}
}
