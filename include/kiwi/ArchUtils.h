#pragma once

namespace kiwi
{
	enum class ArchType
	{
		default_,
		none,
		balanced,
		sse2,
		sse4_1,
		avx2,
		avx512bw,
		neon,
		last = neon,
	};

	ArchType getBestArch();

	ArchType getSelectedArch(ArchType arch);
}
