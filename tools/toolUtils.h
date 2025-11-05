#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <string_view>

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

	inline kiwi::ModelType parseModelType(const std::string& v)
	{
		if (v == "none")
		{
			return kiwi::ModelType::none;
		}
		else if (v == "largest")
		{
			return kiwi::ModelType::largest;
		}
		else if (v == "knlm")
		{
			return kiwi::ModelType::knlm;
		}
		else if (v == "sbg")
		{
			return kiwi::ModelType::sbg;
		}
		else if (v == "knlm-transposed")
		{
			return kiwi::ModelType::knlmTransposed;
		}
		else if (v == "cong")
		{
			return kiwi::ModelType::cong;
		}
		else if (v == "cong-global")
		{
			return kiwi::ModelType::congGlobal;
		}
		else if (v == "cong-fp32")
		{
			return kiwi::ModelType::congFp32;
		}
		else if (v == "cong-global-fp32")
		{
			return kiwi::ModelType::congGlobalFp32;
		}
		else
		{
			throw std::invalid_argument{ "Invalid model type" };
		}
	}


	template<class BaseStr, class BaseChr, class OutIterator>
	OutIterator split(BaseStr&& s, BaseChr delim, OutIterator result, size_t maxSplit = -1, BaseChr delimEscape = 0)
	{
		size_t p = 0, e = 0;
		for (size_t i = 0; i < maxSplit; ++i)
		{
			size_t t = s.find(delim, p);
			if (t == s.npos)
			{
				*(result++) = std::basic_string_view<BaseChr>{ &s[e] , s.size() - e };
				return result;
			}
			else
			{
				if (delimEscape && delimEscape != delim && t > 0 && s[t - 1] == delimEscape)
				{
					p = t + 1;
				}
				else if (delimEscape && delimEscape == delim && t < s.size() - 1 && s[t + 1] == delimEscape)
				{
					p = t + 2;
				}
				else
				{
					*(result++) = std::basic_string_view<BaseChr>{ &s[e] , t - e };
					p = t + 1;
					e = t + 1;
				}
			}
		}
		*(result++) = std::basic_string_view<BaseChr>{ &s[e] , s.size() - e };
		return result;
	}

	template<class BaseChr, class Trait>
	inline std::vector<std::basic_string_view<BaseChr, Trait>> split(std::basic_string_view<BaseChr, Trait> s, BaseChr delim, BaseChr delimEscape = 0)
	{
		std::vector<std::basic_string_view<BaseChr, Trait>> ret;
		split(s, delim, std::back_inserter(ret), -1, delimEscape);
		return ret;
	}

	template<class BaseChr, class Trait, class Alloc>
	inline std::vector<std::basic_string_view<BaseChr, Trait>> split(const std::basic_string<BaseChr, Trait, Alloc>& s, BaseChr delim, BaseChr delimEscape = 0)
	{
		std::vector<std::basic_string_view<BaseChr, Trait>> ret;
		split(s, delim, std::back_inserter(ret), -1, delimEscape);
		return ret;
	}

}
