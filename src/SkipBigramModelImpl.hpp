#pragma once

#include <cmath>
#include "SkipBigramModel.hpp"
#include "SIMD.hpp"

namespace kiwi
{
	namespace lm
	{
		template<ArchType arch>
		struct LogSumExp
		{
			template<size_t size>
			float operator()(const float* arr, std::integral_constant<size_t, size>)
			{
				return logSumExpImpl<arch, size>(arr);
			}
		};
		
		template<>
		struct LogSumExp<ArchType::none>
		{
			template<size_t size>
			float operator()(const float* arr, std::integral_constant<size_t, size>)
			{
				float maxValue = *std::max_element(arr, arr + size);
				float sum = 0;
				for (size_t i = 0; i < size; ++i)
				{
					sum += std::exp(arr[i] - maxValue);
				}
				return std::log(sum) + maxValue;
			}
		};

		template<>
		struct LogSumExp<ArchType::balanced> : public LogSumExp<ArchType::none>
		{
		};

		template<ArchType archType, size_t size>
		float logSumExpImpl(const float* arr)
		{
			if ((archType == ArchType::avx512bw || archType == ArchType::avx512vnni) && size < 16)
			{
				return logSumExpImpl<ArchType::avx2, size>(arr);
			}
			simd::Operator<archType> op;

			auto pmax = op.loadf(arr);
			for (size_t i = op.packetSize; i < size; i += op.packetSize)
			{
				pmax = op.maxf(pmax, op.loadf(&arr[i]));
			}
			pmax = op.redmaxbf(pmax);

			auto sum = op.zerof();
			for (size_t i = 0; i < size; i += op.packetSize)
			{
				sum = op.addf(sum, op.expf(op.subf(op.loadf(&arr[i]), pmax)));
			}
			return std::log(op.redsumf(sum)) + op.firstf(pmax);
		}

		template<ArchType arch>
		struct LogSoftmax
		{
			template<size_t size>
			void operator()(float* arr, std::integral_constant<size_t, size>)
			{
				return logSoftmaxImpl<arch, size>(arr);
			}
		};

		template<>
		struct LogSoftmax<ArchType::none>
		{
			template<size_t size>
			void operator()(float* arr, std::integral_constant<size_t, size>)
			{
				float maxValue = *std::max_element(arr, arr + size);
				float sum = 0;
				for (size_t i = 0; i < size; ++i)
				{
					sum += std::exp(arr[i] - maxValue);
				}
				maxValue += std::log(sum);
				for (size_t i = 0; i < size; ++i)
				{
					arr[i] -= maxValue;
				}
			}
		};

		template<>
		struct LogSoftmax<ArchType::balanced> : public LogSoftmax<ArchType::none>
		{
		};

		template<ArchType archType, size_t size>
		void logSoftmaxImpl(float* arr)
		{
			if ((archType == ArchType::avx512bw || archType == ArchType::avx512vnni) && size < 16)
			{
				return logSoftmaxImpl<ArchType::avx2, size>(arr);
			}
			simd::Operator<archType> op;

			auto pmax = op.loadf(arr);
			for (size_t i = op.packetSize; i < size; i += op.packetSize)
			{
				pmax = op.maxf(pmax, op.loadf(&arr[i]));
			}
			pmax = op.redmaxbf(pmax);

			auto sum = op.zerof();
			for (size_t i = 0; i < size; i += op.packetSize)
			{
				sum = op.addf(sum, op.expf(op.subf(op.loadf(&arr[i]), pmax)));
			}
			pmax = op.addf(op.logf(op.set1f(op.redsumf(sum))), pmax);
			for (size_t i = 0; i < size; i += op.packetSize)
			{
				op.storef(&arr[i], op.subf(op.loadf(&arr[i]), pmax));
			}
		}

		template<ArchType archType, size_t size>
		struct LogSoftmaxTransposed;

		template<ArchType archType>
		struct LogSoftmaxTransposed<archType, 8>
		{
			static constexpr size_t size = 8;

			void block(float* arr, size_t stride)
			{
				simd::Operator<archType> op;
				simd::FloatPacket<archType> a0 = op.loadf(arr),
					a1 = op.loadf(arr + stride),
					a2 = op.loadf(arr + stride * 2),
					a3 = op.loadf(arr + stride * 3),
					a4 = op.loadf(arr + stride * 4),
					a5 = op.loadf(arr + stride * 5),
					a6 = op.loadf(arr + stride * 6),
					a7 = op.loadf(arr + stride * 7);
				// find maximum
				auto m = op.maxf(a0, a1);
				m = op.maxf(m, a2);
				m = op.maxf(m, a3);
				m = op.maxf(m, a4);
				m = op.maxf(m, a5);
				m = op.maxf(m, a6);
				m = op.maxf(m, a7);
				
				// subtract maximum
				a0 = op.subf(a0, m);
				a1 = op.subf(a1, m);
				a2 = op.subf(a2, m);
				a3 = op.subf(a3, m);
				a4 = op.subf(a4, m);
				a5 = op.subf(a5, m);
				a6 = op.subf(a6, m);
				a7 = op.subf(a7, m);
				
				// exp, reduce sum and log
				m = op.expf(a0);
				m = op.addf(m, op.expf(a1));
				m = op.addf(m, op.expf(a2));
				m = op.addf(m, op.expf(a3));
				m = op.addf(m, op.expf(a4));
				m = op.addf(m, op.expf(a5));
				m = op.addf(m, op.expf(a6));
				m = op.addf(m, op.expf(a7));
				m = op.logf(m);
				
				// subtract
				a0 = op.subf(a0, m);
				a1 = op.subf(a1, m);
				a2 = op.subf(a2, m);
				a3 = op.subf(a3, m);
				a4 = op.subf(a4, m);
				a5 = op.subf(a5, m);
				a6 = op.subf(a6, m);
				a7 = op.subf(a7, m);
				
				op.storef(arr, a0);
				op.storef(arr + stride, a1);
				op.storef(arr + stride * 2, a2);
				op.storef(arr + stride * 3, a3);
				op.storef(arr + stride * 4, a4);
				op.storef(arr + stride * 5, a5);
				op.storef(arr + stride * 6, a6);
				op.storef(arr + stride * 7, a7);
			}

			void operator()(float* arr, size_t batchSize, size_t stride)
			{
				simd::Operator<archType> op;
				for (size_t i = 0; i < batchSize; i += op.packetSize)
				{
					block(arr, stride);
					arr += op.packetSize;
				}
			}
		};

		template<>
		struct LogSoftmaxTransposed<ArchType::none, 8> : public LogSoftmaxTransposed<ArchType::sse2, 8>
		{
		};

		template<>
		struct LogSoftmaxTransposed<ArchType::balanced, 8> : public LogSoftmaxTransposed<ArchType::sse2, 8>
		{
		};

		template<ArchType archType, size_t size>
		struct LogSumExpTransposed;

		template<ArchType archType>
		struct LogSumExpTransposed<archType, 8>
		{
			static constexpr size_t size = 8;

			void block(float* arr, size_t stride)
			{
				simd::Operator<archType> op;
				simd::FloatPacket<archType> a0 = op.loadf(arr),
					a1 = op.loadf(arr + stride),
					a2 = op.loadf(arr + stride * 2),
					a3 = op.loadf(arr + stride * 3),
					a4 = op.loadf(arr + stride * 4),
					a5 = op.loadf(arr + stride * 5),
					a6 = op.loadf(arr + stride * 6),
					a7 = op.loadf(arr + stride * 7);
				// find maximum
				auto m = op.maxf(a0, a1);
				m = op.maxf(m, a2);
				m = op.maxf(m, a3);
				m = op.maxf(m, a4);
				m = op.maxf(m, a5);
				m = op.maxf(m, a6);
				m = op.maxf(m, a7);

				// subtract maximum
				a0 = op.subf(a0, m);
				a1 = op.subf(a1, m);
				a2 = op.subf(a2, m);
				a3 = op.subf(a3, m);
				a4 = op.subf(a4, m);
				a5 = op.subf(a5, m);
				a6 = op.subf(a6, m);
				a7 = op.subf(a7, m);

				// exp, reduce sum and log
				auto s = op.expf(a0);
				s = op.addf(s, op.expf(a1));
				s = op.addf(s, op.expf(a2));
				s = op.addf(s, op.expf(a3));
				s = op.addf(s, op.expf(a4));
				s = op.addf(s, op.expf(a5));
				s = op.addf(s, op.expf(a6));
				s = op.addf(s, op.expf(a7));
				s = op.logf(s);

				op.storef(arr, op.addf(m, s));
			}

			void operator()(float* arr, size_t batchSize, size_t stride)
			{
				simd::Operator<archType> op;
				for (size_t i = 0; i < batchSize; i += op.packetSize)
				{
					block(arr, stride);
					arr += op.packetSize;
				}
			}
		};

		template<>
		struct LogSumExpTransposed<ArchType::none, 8> : public LogSumExpTransposed<ArchType::sse2, 8>
		{
		};

		template<>
		struct LogSumExpTransposed<ArchType::balanced, 8> : public LogSumExpTransposed<ArchType::sse2, 8>
		{
		};

		template<ArchType arch, class KeyType, size_t windowSize>
		float SkipBigramModel<arch, KeyType, windowSize>::evaluate(const KeyType* history, size_t cnt, KeyType next, float base) const
		{
			if (!cnt) return base;
			if (!vocabValidness[next]) return base;

#if defined(__GNUC__) && __GNUC__ < 5
			alignas(256) float arr[windowSize * 2];
#else
			alignas(ArchInfo<arch>::alignment) float arr[windowSize * 2];
#endif
			std::fill(arr, arr + windowSize, base);
			std::fill(arr + windowSize, arr + windowSize * 2, -INFINITY);

			size_t b = ptrs[next], e = ptrs[next + 1];
			size_t size = e - b;

			for (size_t i = 0; i < cnt; ++i)
			{
				arr[i] = discnts[history[i]] + base;
				float out;
				if (nst::search<arch>(&keyData[b], &compensations[b], size, history[i], out))
				{
					arr[i + windowSize] = out;
				}
			}
			return LogSumExp<arch>{}(arr, std::integral_constant<size_t, windowSize * 2>{}) - logWindowSize;
		}
	}
}
