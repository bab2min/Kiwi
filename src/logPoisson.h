#pragma once

#include <array>
#include <cmath>

namespace kiwi
{
	class LogPoisson
	{
	private:
		static constexpr size_t maxN = 32;
		std::array<float, maxN> logNFact;
		LogPoisson()
		{
			float sum = 0;
			for (int i = 0; i < 32; i++)
			{
				logNFact[i] = sum += log(i + 1);
			}
		}
	public:
		static LogPoisson& getInstance()
		{
			static LogPoisson inst;
			return inst;
		}

		static float getLL(float lambda, size_t n)
		{
			if (!n || n > maxN) return -100;
			return log(lambda) * n - lambda - getInstance().logNFact[n - 1];
		}
	};
}