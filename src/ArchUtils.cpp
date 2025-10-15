#include <cstdlib>
#include <cstdio>
#include <string>
#include <algorithm>

#include "ArchAvailable.h"

using namespace kiwi;

ArchType kiwi::getBestArch()
{
#ifdef KIWI_USE_CPUINFO
	cpuinfo_initialize();
#if CPUINFO_ARCH_X86_64
	if (cpuinfo_has_x86_avx512vnni()) return ArchType::avx512vnni;
	if (cpuinfo_has_x86_avx512bw()) return ArchType::avx512bw;
#ifdef KIWI_AVX_VNNI_SUPPORTED
	if (cpuinfo_has_x86_avx_vnni_int8()) return ArchType::avx_vnni;
#endif
	if (cpuinfo_has_x86_avx2()) return ArchType::avx2;
	if (cpuinfo_has_x86_sse4_1()) return ArchType::sse4_1;
#endif
#if CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64
	if (cpuinfo_has_x86_sse2()) return ArchType::sse2;
#endif
#if CPUINFO_ARCH_ARM64
	if (cpuinfo_has_arm_neon()) return ArchType::neon;
#endif
#else
#ifdef KIWI_ARCH_X86_64
	return ArchType::avx512vnni;
#elif defined(__x86_64__) || defined(KIWI_ARCH_X86)
	return ArchType::sse2;
#elif defined(KIWI_ARCH_ARM64)
	return ArchType::neon;
#endif
#endif
	return ArchType::none;
}

namespace kiwi
{
	static const char* archNames[] = {
		"default",
		"none",
		"balanced",
		"sse2",
		"sse4_1",
		"avx2",
		"avx_vnni",
		"avx512bw",
		"avx512vnni",
		"neon",
	};

	static ArchType testArchSet(ArchType arch, ArchType best)
	{
		if (arch <= ArchType::balanced) return arch;
#if !defined(KIWI_AVX_VNNI_SUPPORTED)
		if (arch == ArchType::avx_vnni && best >= ArchType::avx_vnni)
		{
			std::fprintf(stderr, "This binary isn't built with AVX VNNI support. ArchType::avx2 will be used instead.\n");
			return ArchType::avx2;
		}
#endif
#if CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64 || KIWI_ARCH_X86_64 || KIWI_ARCH_X86
		if (ArchType::sse2 <= arch && arch <= ArchType::avx512vnni && arch <= best)
		{
			return arch;
		}
#elif CPUINFO_ARCH_ARM || CPUINFO_ARCH_ARM64 || KIWI_ARCH_NEON
		if (ArchType::neon <= arch && arch <= ArchType::neon && arch <= best)
		{
			return arch;
		}
#endif
		std::fprintf(stderr, "ArchType::%s is not supported in this environment. ArchType::%s will be used instead.\n",
			archNames[static_cast<int>(arch)],
			archNames[static_cast<int>(best)]
		);
		return best;
	}

	inline char asciitolower(char in) {
		if (in <= 'Z' && in >= 'A')
			return in - ('Z' - 'z');
		return in;
	}

	inline ArchType parseArchType(const char* env)
	{
		std::string envs = env;
		std::transform(envs.begin(), envs.end(), envs.begin(), asciitolower);

		for (size_t i = 0; i <= static_cast<size_t>(ArchType::last); ++i)
		{
			if (envs == archNames[i]) return static_cast<ArchType>(i);
		}

		std::fprintf(stderr, "Wrong value for KIWI_ARCH_TYPE: %s\nArchType::default will be used instead.\n", env);
		return ArchType::default_;
	}
}

ArchType kiwi::getSelectedArch(ArchType arch)
{
	static ArchType best = getBestArch();
	if (arch == ArchType::default_)
	{
		const char* env = std::getenv("KIWI_ARCH_TYPE");
		if (!env) return best;
		arch = parseArchType(env);
		if (arch == ArchType::default_) return best;
	}
	arch = testArchSet(arch, best);
	return arch;
}

const char* kiwi::archToStr(ArchType arch)
{
	if (arch <= ArchType::last) 
		return archNames[static_cast<size_t>(arch)];

	return "unknown";
}
