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
		avx_vnni,
		avx512bw,
		avx512vnni,
		neon,
		last = neon,
	};

	template<ArchType>
	struct ArchTypeHolder {};

	ArchType getBestArch();

	ArchType getSelectedArch(ArchType arch);

	const char* archToStr(ArchType arch);

	template<ArchType arch>
	struct ArchInfo
	{
		static constexpr size_t alignment = 4;
	};

	template<>
	struct ArchInfo<ArchType::sse2>
	{
		static constexpr size_t alignment = 16;
	};

	template<>
	struct ArchInfo<ArchType::sse4_1>
	{
		static constexpr size_t alignment = 16;
	};

	template<>
	struct ArchInfo<ArchType::avx2>
	{
		static constexpr size_t alignment = 32;
	};

	template<>
	struct ArchInfo<ArchType::avx_vnni>
	{
		static constexpr size_t alignment = 32;
	};

	template<>
	struct ArchInfo<ArchType::avx512bw>
	{
		static constexpr size_t alignment = 64;
	};

	template<>
	struct ArchInfo<ArchType::avx512vnni>
	{
		static constexpr size_t alignment = 64;
	};

	template<>
	struct ArchInfo<ArchType::neon>
	{
		static constexpr size_t alignment = 16;
	};
}
