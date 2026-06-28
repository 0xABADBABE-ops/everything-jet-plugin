// Minimal IPC test — checks if the Everything3 SDK v3 can communicate with Everything.
#include <cstdio>
#include <windows.h>
#define EVERYTHING3_USERAPI
#include "Everything3.h"

int main() {
    // Connect via IPC3 named pipe
    EVERYTHING3_CLIENT* client = Everything3_Connect(NULL);
    if (!client) {
        DWORD err = Everything3_GetLastError();
        std::printf("Connect FAILED: 0x%08X\n", err);
        if (err == EVERYTHING3_ERROR_IPC_PIPE_NOT_FOUND) {
            std::printf("Is Everything 1.5 running?\n");
        }
        return 1;
    }

    DWORD major = Everything3_GetMajorVersion(client);
    DWORD minor = Everything3_GetMinorVersion(client);
    DWORD rev   = Everything3_GetRevision(client);
    DWORD build = Everything3_GetBuildNumber(client);
    std::printf("Version: %lu.%lu.%lu.%lu\n", major, minor, rev, build);

    // Test 1: normal query
    std::printf("\nTest 1: Search 'notepad.exe' (viewport=3)...\n");
    std::fflush(stdout);
    {
        EVERYTHING3_SEARCH_STATE* ss = Everything3_CreateSearchState();
        Everything3_SetSearchTextW(ss, L"notepad.exe");
        Everything3_SetSearchViewportCount(ss, 3);

        EVERYTHING3_RESULT_LIST* rl = Everything3_Search(client, ss);
        if (rl) {
            SIZE_T count = Everything3_GetResultListViewportCount(rl);
            SIZE_T total = Everything3_GetResultListCount(rl);
            std::printf("  viewport=%zu total=%zu\n", count, total);
            Everything3_DestroyResultList(rl);
        } else {
            std::printf("  Search FAILED: 0x%08X\n", Everything3_GetLastError());
        }
        Everything3_DestroySearchState(ss);
    }

    // Test 2: impossible query
    std::printf("\nTest 2: Search 'zzz_nope_xyz.zzz'...\n");
    std::fflush(stdout);
    {
        EVERYTHING3_SEARCH_STATE* ss = Everything3_CreateSearchState();
        Everything3_SetSearchTextW(ss, L"zzz_nope_xyz.zzz");

        EVERYTHING3_RESULT_LIST* rl = Everything3_Search(client, ss);
        if (rl) {
            SIZE_T count = Everything3_GetResultListViewportCount(rl);
            SIZE_T total = Everything3_GetResultListCount(rl);
            std::printf("  viewport=%zu total=%zu\n", count, total);
            Everything3_DestroyResultList(rl);
        } else {
            std::printf("  Search FAILED: 0x%08X\n", Everything3_GetLastError());
        }
        Everything3_DestroySearchState(ss);
    }

    // Test 3: empty query (should NOT hang in v3)
    std::printf("\nTest 3: Empty query (viewport=1)...\n");
    std::fflush(stdout);
    {
        EVERYTHING3_SEARCH_STATE* ss = Everything3_CreateSearchState();
        Everything3_SetSearchTextW(ss, L"");
        Everything3_SetSearchViewportCount(ss, 1);

        EVERYTHING3_RESULT_LIST* rl = Everything3_Search(client, ss);
        if (rl) {
            SIZE_T total = Everything3_GetResultListCount(rl);
            std::printf("  total=%zu (no hang!)\n", total);
            Everything3_DestroyResultList(rl);
        } else {
            std::printf("  Search FAILED: 0x%08X\n", Everything3_GetLastError());
        }
        Everything3_DestroySearchState(ss);
    }

    Everything3_DestroyClient(client);
    std::printf("\nDone.\n");
    return 0;
}
