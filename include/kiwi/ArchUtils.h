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

	template<ArchType>
	struct ArchTypeHolder {};

	ArchType getBestArch();

	ArchType getSelectedArch(ArchType arch);

	const char* archToStr(ArchType arch);
}
