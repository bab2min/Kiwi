#pragma once
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include "MathFunc.h"
#include "SIMD.hpp"

namespace kiwi
{
	namespace lm
	{
		template<ArchType archType, size_t size>
		float logSumExpImpl(const float* arr)
		{
			static constexpr ArchType bestArchType = simd::BestArchType<archType, size>::value;
			simd::Operator<bestArchType> op;

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
		void logSoftmaxImpl(float* arr)
		{
			static constexpr ArchType bestArchType = simd::BestArchType<archType, size>::value;
			simd::Operator<bestArchType> op;

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
		struct LogSoftmaxTransposed<ArchType::none, 8>
		{
			void compute(float* arr, size_t stride)
			{
				float maxValue = -INFINITY;
				for (size_t i = 0; i < 8; ++i)
				{
					maxValue = std::max(maxValue, arr[i * stride]);
				}
				float sum = 0;
				for (size_t i = 0; i < 8; ++i)
				{
					sum += std::exp(arr[i * stride] - maxValue);
				}
				maxValue += std::log(sum);
				for (size_t i = 0; i < 8; ++i)
				{
					arr[i * stride] -= maxValue;
				}
			}

			void operator()(float* arr, size_t batchSize, size_t stride)
			{
				for (size_t i = 0; i < batchSize; ++i)
				{
					compute(arr + i, stride);
				}
			}
		};

		template<>
		struct LogSoftmaxTransposed<ArchType::balanced, 8> : public LogSoftmaxTransposed<ArchType::none, 8>
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
		struct LogSumExpTransposed<ArchType::none, 8>
		{
			void compute(float* arr, size_t stride)
			{
				float maxValue = -INFINITY;
				for (size_t i = 0; i < 8; ++i)
				{
					maxValue = std::max(maxValue, arr[i * stride]);
				}
				float sum = 0;
				for (size_t i = 0; i < 8; ++i)
				{
					sum += std::exp(arr[i * stride] - maxValue);
				}
				*arr = std::log(sum) + maxValue;
			}

			void operator()(float* arr, size_t batchSize, size_t stride)
			{
				for (size_t i = 0; i < batchSize; ++i)
				{
					compute(arr + i, stride);
				}
			}
		};

		template<>
		struct LogSumExpTransposed<ArchType::balanced, 8> : public LogSumExpTransposed<ArchType::none, 8>
		{
		};

		template<ArchType archType>
		float logSumExp(const float* arr, size_t size)
		{
			if (size == 8) return LogSumExp<archType>()(arr, std::integral_constant<size_t, 8>());
			if (size == 16) return LogSumExp<archType>()(arr, std::integral_constant<size_t, 16>());
			throw std::runtime_error("Unsupported size");
		}

		template<ArchType archType>
		void logSumExpTransposed(float* arr, size_t size, size_t batchSize, size_t stride)
		{
			if (size == 8) return LogSumExpTransposed<archType, 8>{}(arr, batchSize, stride);
			throw std::runtime_error("Unsupported size");
		}

		template<ArchType archType>
		void logSoftmax(float* arr, size_t size)
		{
			if (size == 8) return LogSoftmax<archType>()(arr, std::integral_constant<size_t, 8>());
			if (size == 16) return LogSoftmax<archType>()(arr, std::integral_constant<size_t, 16>());
			throw std::runtime_error("Unsupported size");
		}

		template<ArchType archType>
		void logSoftmaxTransposed(float* arr, size_t size, size_t batchSize, size_t stride)
		{
			if (size == 8) return LogSoftmaxTransposed<archType, 8>{}(arr, batchSize, stride);
			throw std::runtime_error("Unsupported size");
		}
	}
}
