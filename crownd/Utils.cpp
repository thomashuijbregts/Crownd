#include "Utils.h"

#if defined(_WIN32)
	#include <windows.h>
	U64 utils::availableMemory() {
		MEMORYSTATUSEX mem{};
		mem.dwLength = sizeof(mem);
		GlobalMemoryStatusEx(&mem);
		return mem.ullAvailPhys;
	}
#elif defined(__linux__)
	#include <unistd.h>

	U64 utils::availableMemory() {
		return U64(sysconf(_SC_AVPHYS_PAGES)) * U64(sysconf(_SC_PAGE_SIZE));
	}
#else
	U64 utils::availableMemory() {
		return 512;
	}
#endif