#pragma once
#include <Eigen/Dense>
#include "serializer.hpp"

namespace kiwi
{
	namespace serializer
	{
		template<class Scalar, int rows, int cols, int options>
		struct Serializer<Eigen::Array<Scalar, rows, cols, options>>
		{
			using VTy = Eigen::Array<Scalar, rows, cols, options>;
			void write(std::ostream& ostr, const VTy& v)
			{
				int nrows = v.rows(), ncols = v.cols();
				writeMany(ostr, nrows, ncols);
				ostr.write((const char*)v.data(), sizeof(Scalar) * nrows * ncols);
			}

			void read(std::istream& istr, VTy& v)
			{
				int nrows, ncols;
				readMany(istr, nrows, ncols);
				v.resize(nrows, ncols);
				istr.read((char*)v.data(), sizeof(Scalar) * nrows * ncols);
			}
		};
	}
}