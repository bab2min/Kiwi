#pragma once

#ifdef KIWI_USE_CPUINFO
#include <cpuinfo.h>
#endif

#include <kiwi/ArchUtils.h>
#include <kiwi/TemplateUtils.hpp>

namespace kiwi
{
	using AvailableArch = tp::seq<
#ifdef KIWI_USE_CPUINFO
#if CPUINFO_ARCH_X86_64
		static_cast<std::ptrdiff_t>(ArchType::avx512bw),
		static_cast<std::ptrdiff_t>(ArchType::avx2),
		static_cast<std::ptrdiff_t>(ArchType::sse4_1),
#endif
#if CPUINFO_ARCH_X86 || CPUINFO_ARCH_X86_64
		static_cast<std::ptrdiff_t>(ArchType::sse2),
#endif
#if CPUINFO_ARCH_ARM64
		static_cast<std::ptrdiff_t>(ArchType::neon),
#endif
#else
#ifdef KIWI_ARCH_X86_64
		static_cast<std::ptrdiff_t>(ArchType::avx512bw),
		static_cast<std::ptrdiff_t>(ArchType::avx2),
		static_cast<std::ptrdiff_t>(ArchType::sse4_1),
#endif
#if defined(__x86_64__) || defined(KIWI_ARCH_X86) || defined(KIWI_ARCH_X86_64)
		static_cast<std::ptrdiff_t>(ArchType::sse2),
#endif
#ifdef KIWI_ARCH_ARM64
		static_cast<std::ptrdiff_t>(ArchType::neon),
#endif
#endif
		static_cast<std::ptrdiff_t>(ArchType::none),
		static_cast<std::ptrdiff_t>(ArchType::balanced)
	>;
}
