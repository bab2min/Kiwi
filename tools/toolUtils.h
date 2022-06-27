#pragma once

#include <chrono>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <tchar.h>
#include <fcntl.h>
#include <io.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#else
#include <cstring>
#include <cstdio>
#endif

namespace tutils
{
	class Timer
	{
	public:
		std::chrono::high_resolution_clock::time_point point;
		Timer()
		{
			reset();
		}

		void reset()
		{
			point = std::chrono::high_resolution_clock::now();
		}

		double getElapsed() const
		{
			return std::chrono::duration <double, std::milli>(std::chrono::high_resolution_clock::now() - point).count();
		}
	};

#ifdef _WIN32
	inline size_t getCurrentPhysicalMemoryUsage()
	{
		PROCESS_MEMORY_COUNTERS pmc;
		GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
		return (pmc.WorkingSetSize + 512) / 1024;
	}

	inline void setUTF8Output()
	{
		SetConsoleOutputCP(CP_UTF8);
		setvbuf(stdout, nullptr, _IOFBF, 1000);
		_setmode(_fileno(stdin), _O_U16TEXT);
	}
#elif defined(__APPLE__)
	inline size_t getCurrentPhysicalMemoryUsage()
	{
		task_basic_info t_info;
		mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;
		if (KERN_SUCCESS != task_info(mach_task_self(),
			TASK_BASIC_INFO, (task_info_t)&t_info,
			&t_info_count)
		) return 0;

		return (size_t)t_info.resident_size / 1024;
	}

	inline void setUTF8Output()
	{
	}
#else
	namespace detail
	{
		inline int parseLine(char* line)
		{
			int i = strlen(line);
			const char* p = line;
			while (*p < '0' || *p > '9') p++;
			line[i - 3] = '\0';
			i = atoi(p);
			return i;
		}
	}

	inline size_t getCurrentPhysicalMemoryUsage()
	{
		FILE* file = fopen("/proc/self/status", "r");
		int result = -1;
		char line[128];

		while (fgets(line, 128, file) != NULL) {
			if (strncmp(line, "VmSize:", 7) == 0) {
				result = detail::parseLine(line);
				break;
			}
		}
		fclose(file);
		return result;
	}

	inline void setUTF8Output()
	{
	}
#endif
}