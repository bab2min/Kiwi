#pragma once
#include <algorithm>
#include <kiwi/Types.h>

namespace kiwi
{
	namespace nuq
	{
		template<class Ty>
		inline Ty average(const Ty* f, size_t n)
		{
			Ty sum = 0;
			for (size_t i = 0; i < n; ++i) sum += f[i];
			return sum / n;
		}

		template<class Ty>
		inline Ty sumSquaredErrors(const Ty* f, size_t n, Ty mean)
		{
			Ty sum = 0;
			for (size_t i = 0; i < n; ++i) sum += (f[i] - mean) * (f[i] - mean);
			return sum;
		}

		template<class Ty, class VecTy>
		inline Ty nuquant(Ty* cs, const VecTy& vs, size_t cats)
		{
			const size_t cols = vs.size();
			if (cols <= cats)
			{
				for (size_t i = 0; i < cols; ++i) cs[i] = vs[i];
				for (size_t i = cols; i < cats; ++i) cs[i] = cs[cols - 1];
				return 0.f;
			}

			if (cats == 1)
			{
				float c = average(&vs[0], cols);
				cs[0] = c;
				return sumSquaredErrors(&vs[0], cols, c) / cols;
			}

			Vector<size_t> boundary(cats + 1), best_boundary;
			for (int i = 1; i <= cats; ++i)
			{
				boundary[i] = i * cols / cats;
				cs[i - 1] = average(&vs[boundary[i - 1]], boundary[i] - boundary[i - 1]);
			}

			Ty best_mse = INFINITY;
			size_t deadline = 10;
			for (size_t epoch = 0; epoch < deadline && epoch < 1000; ++epoch)
			{
				// update boundaries
				for (size_t i = 1; i < cats; ++i)
				{
					float mid = (cs[i - 1] + cs[i]) / 2;
					boundary[i] = std::find_if(vs.begin() + boundary[i - 1], vs.end(), [&](Ty x)
						{
							return x > mid;
						}) - vs.begin();
				}

				boundary.erase(std::unique(boundary.begin(), boundary.end()), boundary.end());
				while (boundary.size() <= cats)
				{
					Ty max_diff = 0;
					size_t max_i = 0;
					for (size_t i = 1; i < boundary.size(); ++i)
					{
						if (boundary[i] - boundary[i - 1] <= 1) continue;
						Ty diff = vs[boundary[i] - 1] - vs[boundary[i - 1]];
						if (diff > max_diff)
						{
							max_diff = diff;
							max_i = i;
						}
					}
					boundary.insert(boundary.begin() + max_i, (boundary[max_i - 1] + boundary[max_i]) / 2);
				}

				Ty mse = 0;
				// update centroids
				for (size_t i = 0; i < cats; ++i)
				{
					cs[i] = average(&vs[boundary[i]], boundary[i + 1] - boundary[i]);
					mse += sumSquaredErrors(&vs[boundary[i]], boundary[i + 1] - boundary[i], cs[i]);
				}

				if (mse < best_mse)
				{
					best_mse = mse;
					best_boundary = boundary;
					deadline = epoch + 10;
				}
			}

			for (size_t i = 0; i < cats; ++i)
			{
				cs[i] = average(&vs[best_boundary[i]], best_boundary[i + 1] - best_boundary[i]);
			}
			return best_mse / cols;
		}

		template<class Float = float>
		class NUQuantizer
		{
			Vector<Float> bounds;
		public:
			template<class It>
			NUQuantizer(It first, It last)
				: bounds(std::distance(first, last) - 1)
			{
				for (size_t i = 0; i < bounds.size(); ++i)
				{
					bounds[i] = (first[i] + first[i + 1]) / 2;
				}
			}

			size_t operator()(Float v) const
			{
				return (size_t)(std::lower_bound(bounds.begin(), bounds.end(), v) - bounds.begin());
			}
		};
	}
}
