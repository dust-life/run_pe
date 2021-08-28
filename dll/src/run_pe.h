#pragma once

#include <windows.h>

/**
Perform the RunPE injection of the payload into the target.
*/
bool run_pe(IN BYTE* payload, IN size_t r_size, OUT size_t& v_size, IN const char* targetPath, IN const char* cmdLine);
BYTE* memory_load(IN BYTE* payload, IN size_t r_size, OUT size_t& v_size, bool executable, bool relocate);
