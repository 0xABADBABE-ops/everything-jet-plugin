//
// everything_wrapper.cpp — Implementation of the Everything3 SDK v3 C++ wrapper.
// Dynamically loads Everything3_x64.dll, manages a persistent IPC3 named-pipe
// client connection, and provides clean search with structured result objects.
//

#include "everything_wrapper.hpp"
#include "string_utils.hpp"

#include <windows.h>
#include "Everything3.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// Everything3 SDK function pointer types
// PFN names match the DLL export names exactly (Everything3_*) so the
// RESOLVE macro can construct the PFN type and export string from one arg.
#define DECLARE_PFN(name, sig) typedef sig PFN_##name

typedef EVERYTHING3_DWORD  (EVERYTHING3_API *PFN_Everything3_GetLastError)(void);
typedef EVERYTHING3_CLIENT*(EVERYTHING3_API *PFN_Everything3_ConnectW)(const EVERYTHING3_WCHAR*);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_ShutdownClient)(EVERYTHING3_CLIENT*);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_DestroyClient)(EVERYTHING3_CLIENT*);
typedef EVERYTHING3_DWORD  (EVERYTHING3_API *PFN_Everything3_GetMajorVersion)(EVERYTHING3_CLIENT*);
typedef EVERYTHING3_DWORD  (EVERYTHING3_API *PFN_Everything3_GetMinorVersion)(EVERYTHING3_CLIENT*);
typedef EVERYTHING3_DWORD  (EVERYTHING3_API *PFN_Everything3_GetRevision)(EVERYTHING3_CLIENT*);
typedef EVERYTHING3_DWORD  (EVERYTHING3_API *PFN_Everything3_GetBuildNumber)(EVERYTHING3_CLIENT*);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_IsDBLoaded)(EVERYTHING3_CLIENT*);
// Search state
typedef EVERYTHING3_SEARCH_STATE*(EVERYTHING3_API *PFN_Everything3_CreateSearchState)(void);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_DestroySearchState)(EVERYTHING3_SEARCH_STATE*);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_SetSearchMatchCase)(EVERYTHING3_SEARCH_STATE*, EVERYTHING3_BOOL);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_SetSearchMatchWholeWords)(EVERYTHING3_SEARCH_STATE*, EVERYTHING3_BOOL);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_SetSearchMatchPath)(EVERYTHING3_SEARCH_STATE*, EVERYTHING3_BOOL);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_SetSearchRegex)(EVERYTHING3_SEARCH_STATE*, EVERYTHING3_BOOL);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_SetSearchTextW)(EVERYTHING3_SEARCH_STATE*, const EVERYTHING3_WCHAR*);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_SetSearchViewportOffset)(EVERYTHING3_SEARCH_STATE*, EVERYTHING3_SIZE_T);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_SetSearchViewportCount)(EVERYTHING3_SEARCH_STATE*, EVERYTHING3_SIZE_T);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_SetSearchRequestTotalSize)(EVERYTHING3_SEARCH_STATE*, EVERYTHING3_BOOL);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_AddSearchSort)(EVERYTHING3_SEARCH_STATE*, EVERYTHING3_DWORD, EVERYTHING3_BOOL);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_ClearSearchSorts)(EVERYTHING3_SEARCH_STATE*);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_AddSearchPropertyRequest)(EVERYTHING3_SEARCH_STATE*, EVERYTHING3_DWORD);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_ClearSearchPropertyRequests)(EVERYTHING3_SEARCH_STATE*);
// Execute
typedef EVERYTHING3_RESULT_LIST*(EVERYTHING3_API *PFN_Everything3_Search)(EVERYTHING3_CLIENT*, EVERYTHING3_SEARCH_STATE*);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_DestroyResultList)(EVERYTHING3_RESULT_LIST*);
// Result list
typedef EVERYTHING3_SIZE_T (EVERYTHING3_API *PFN_Everything3_GetResultListCount)(const EVERYTHING3_RESULT_LIST*);
typedef EVERYTHING3_UINT64 (EVERYTHING3_API *PFN_Everything3_GetResultListTotalSize)(const EVERYTHING3_RESULT_LIST*);
typedef EVERYTHING3_SIZE_T (EVERYTHING3_API *PFN_Everything3_GetResultListViewportCount)(const EVERYTHING3_RESULT_LIST*);
typedef EVERYTHING3_BOOL   (EVERYTHING3_API *PFN_Everything3_IsFolderResult)(const EVERYTHING3_RESULT_LIST*, EVERYTHING3_SIZE_T);
typedef EVERYTHING3_SIZE_T (EVERYTHING3_API *PFN_Everything3_GetResultNameW)(const EVERYTHING3_RESULT_LIST*, EVERYTHING3_SIZE_T, EVERYTHING3_WCHAR*, EVERYTHING3_SIZE_T);
typedef EVERYTHING3_SIZE_T (EVERYTHING3_API *PFN_Everything3_GetResultPathW)(const EVERYTHING3_RESULT_LIST*, EVERYTHING3_SIZE_T, EVERYTHING3_WCHAR*, EVERYTHING3_SIZE_T);
typedef EVERYTHING3_SIZE_T (EVERYTHING3_API *PFN_Everything3_GetResultFullPathNameW)(const EVERYTHING3_RESULT_LIST*, EVERYTHING3_SIZE_T, EVERYTHING3_WCHAR*, EVERYTHING3_SIZE_T);
typedef EVERYTHING3_SIZE_T (EVERYTHING3_API *PFN_Everything3_GetResultExtensionW)(const EVERYTHING3_RESULT_LIST*, EVERYTHING3_SIZE_T, EVERYTHING3_WCHAR*, EVERYTHING3_SIZE_T);
typedef EVERYTHING3_UINT64 (EVERYTHING3_API *PFN_Everything3_GetResultSize)(const EVERYTHING3_RESULT_LIST*, EVERYTHING3_SIZE_T);
typedef EVERYTHING3_UINT64 (EVERYTHING3_API *PFN_Everything3_GetResultDateModified)(const EVERYTHING3_RESULT_LIST*, EVERYTHING3_SIZE_T);
typedef EVERYTHING3_UINT64 (EVERYTHING3_API *PFN_Everything3_GetResultDateCreated)(const EVERYTHING3_RESULT_LIST*, EVERYTHING3_SIZE_T);
typedef EVERYTHING3_UINT64 (EVERYTHING3_API *PFN_Everything3_GetResultDateAccessed)(const EVERYTHING3_RESULT_LIST*, EVERYTHING3_SIZE_T);
typedef EVERYTHING3_DWORD  (EVERYTHING3_API *PFN_Everything3_GetResultAttributes)(const EVERYTHING3_RESULT_LIST*, EVERYTHING3_SIZE_T);

namespace {

struct EverythingApi {
    HMODULE dll = nullptr;
    std::atomic<bool> loaded{false};

    // Client (persistent connection)
    EVERYTHING3_CLIENT* client = nullptr;
    std::mutex client_mutex;

    PFN_Everything3_GetLastError              GetLastError = nullptr;
    PFN_Everything3_ConnectW                  ConnectW = nullptr;
    PFN_Everything3_ShutdownClient            ShutdownClient = nullptr;
    PFN_Everything3_DestroyClient             DestroyClient = nullptr;
    PFN_Everything3_GetMajorVersion           GetMajorVersion = nullptr;
    PFN_Everything3_GetMinorVersion           GetMinorVersion = nullptr;
    PFN_Everything3_GetRevision               GetRevision = nullptr;
    PFN_Everything3_GetBuildNumber            GetBuildNumber = nullptr;
    PFN_Everything3_IsDBLoaded                IsDBLoaded = nullptr;
    // Search state
    PFN_Everything3_CreateSearchState         CreateSearchState = nullptr;
    PFN_Everything3_DestroySearchState        DestroySearchState = nullptr;
    PFN_Everything3_SetSearchMatchCase        SetSearchMatchCase = nullptr;
    PFN_Everything3_SetSearchMatchWholeWords  SetSearchMatchWholeWords = nullptr;
    PFN_Everything3_SetSearchMatchPath        SetSearchMatchPath = nullptr;
    PFN_Everything3_SetSearchRegex            SetSearchRegex = nullptr;
    PFN_Everything3_SetSearchTextW            SetSearchTextW = nullptr;
    PFN_Everything3_SetSearchViewportOffset   SetSearchViewportOffset = nullptr;
    PFN_Everything3_SetSearchViewportCount    SetSearchViewportCount = nullptr;
    PFN_Everything3_SetSearchRequestTotalSize SetSearchRequestTotalSize = nullptr;
    PFN_Everything3_AddSearchSort             AddSearchSort = nullptr;
    PFN_Everything3_ClearSearchSorts          ClearSearchSorts = nullptr;
    PFN_Everything3_AddSearchPropertyRequest  AddSearchPropertyRequest = nullptr;
    PFN_Everything3_ClearSearchPropertyRequests ClearSearchPropertyRequests = nullptr;
    // Execute
    PFN_Everything3_Search                    Search = nullptr;
    PFN_Everything3_DestroyResultList         DestroyResultList = nullptr;
    // Result list
    PFN_Everything3_GetResultListCount         GetResultListCount = nullptr;
    PFN_Everything3_GetResultListTotalSize     GetResultListTotalSize = nullptr;
    PFN_Everything3_GetResultListViewportCount GetResultListViewportCount = nullptr;
    PFN_Everything3_IsFolderResult             IsFolderResult = nullptr;
    PFN_Everything3_GetResultNameW             GetResultNameW = nullptr;
    PFN_Everything3_GetResultPathW             GetResultPathW = nullptr;
    PFN_Everything3_GetResultFullPathNameW     GetResultFullPathNameW = nullptr;
    PFN_Everything3_GetResultExtensionW        GetResultExtensionW = nullptr;
    PFN_Everything3_GetResultSize              GetResultSize = nullptr;
    PFN_Everything3_GetResultDateModified      GetResultDateModified = nullptr;
    PFN_Everything3_GetResultDateCreated       GetResultDateCreated = nullptr;
    PFN_Everything3_GetResultDateAccessed      GetResultDateAccessed = nullptr;
    PFN_Everything3_GetResultAttributes        GetResultAttributes = nullptr;

    ~EverythingApi() {
        if (client && DestroyClient) DestroyClient(client);
        if (dll) FreeLibrary(dll);
    }
};

EverythingApi& api() {
    static EverythingApi instance;
    if (!instance.loaded) {
        HMODULE h = nullptr;

        // Try standard DLL search
        for (const char* name : {"Everything3_x64.dll", "Everything3.dll"}) {
            h = LoadLibraryA(name);
            if (h) break;
        }

        // Try relative to executable
        if (!h) {
            char module_path[MAX_PATH] = {};
            GetModuleFileNameA(nullptr, module_path, MAX_PATH);
            std::string exe_dir(module_path);
            size_t last_slash = exe_dir.find_last_of("\\/");
            if (last_slash != std::string::npos) exe_dir = exe_dir.substr(0, last_slash);

            for (const char* rel : {
                "\\Everything3_x64.dll",
                "\\..\\SDK-3.0.0.9\\dll\\Everything3_x64.dll",
                "\\..\\..\\SDK-3.0.0.9\\dll\\Everything3_x64.dll"
            }) {
                h = LoadLibraryA((exe_dir + rel).c_str());
                if (h) break;
            }
        }

        if (h) {
            instance.dll = h;
            instance.loaded = true;

            #define RESOLVE(member, func) \
                instance.member = reinterpret_cast<PFN_##func>(GetProcAddress(h, #func))

            RESOLVE(GetLastError,              Everything3_GetLastError);
            RESOLVE(ConnectW,                  Everything3_ConnectW);
            RESOLVE(ShutdownClient,            Everything3_ShutdownClient);
            RESOLVE(DestroyClient,             Everything3_DestroyClient);
            RESOLVE(GetMajorVersion,           Everything3_GetMajorVersion);
            RESOLVE(GetMinorVersion,           Everything3_GetMinorVersion);
            RESOLVE(GetRevision,               Everything3_GetRevision);
            RESOLVE(GetBuildNumber,            Everything3_GetBuildNumber);
            RESOLVE(IsDBLoaded,                Everything3_IsDBLoaded);
            RESOLVE(CreateSearchState,         Everything3_CreateSearchState);
            RESOLVE(DestroySearchState,        Everything3_DestroySearchState);
            RESOLVE(SetSearchMatchCase,        Everything3_SetSearchMatchCase);
            RESOLVE(SetSearchMatchWholeWords,  Everything3_SetSearchMatchWholeWords);
            RESOLVE(SetSearchMatchPath,        Everything3_SetSearchMatchPath);
            RESOLVE(SetSearchRegex,            Everything3_SetSearchRegex);
            RESOLVE(SetSearchTextW,            Everything3_SetSearchTextW);
            RESOLVE(SetSearchViewportOffset,   Everything3_SetSearchViewportOffset);
            RESOLVE(SetSearchViewportCount,    Everything3_SetSearchViewportCount);
            RESOLVE(SetSearchRequestTotalSize, Everything3_SetSearchRequestTotalSize);
            RESOLVE(AddSearchSort,             Everything3_AddSearchSort);
            RESOLVE(ClearSearchSorts,          Everything3_ClearSearchSorts);
            RESOLVE(AddSearchPropertyRequest,  Everything3_AddSearchPropertyRequest);
            RESOLVE(ClearSearchPropertyRequests, Everything3_ClearSearchPropertyRequests);
            RESOLVE(Search,                    Everything3_Search);
            RESOLVE(DestroyResultList,         Everything3_DestroyResultList);
            RESOLVE(GetResultListCount,         Everything3_GetResultListCount);
            RESOLVE(GetResultListTotalSize,     Everything3_GetResultListTotalSize);
            RESOLVE(GetResultListViewportCount, Everything3_GetResultListViewportCount);
            RESOLVE(IsFolderResult,             Everything3_IsFolderResult);
            RESOLVE(GetResultNameW,             Everything3_GetResultNameW);
            RESOLVE(GetResultPathW,             Everything3_GetResultPathW);
            RESOLVE(GetResultFullPathNameW,     Everything3_GetResultFullPathNameW);
            RESOLVE(GetResultExtensionW,        Everything3_GetResultExtensionW);
            RESOLVE(GetResultSize,              Everything3_GetResultSize);
            RESOLVE(GetResultDateModified,      Everything3_GetResultDateModified);
            RESOLVE(GetResultDateCreated,       Everything3_GetResultDateCreated);
            RESOLVE(GetResultDateAccessed,      Everything3_GetResultDateAccessed);
            RESOLVE(GetResultAttributes,        Everything3_GetResultAttributes);

            #undef RESOLVE
        }
    }
    return instance;
}

// Connect (or reconnect) to Everything over IPC3 named pipe.
// Returns nullptr if connection fails.
EVERYTHING3_CLIENT* ensure_client() {
    auto& a = api();
    if (!a.loaded || !a.ConnectW) return nullptr;

    std::lock_guard<std::mutex> lock(a.client_mutex);

    if (a.client) {
        // Verify still alive with a cheap version call
        if (a.GetMajorVersion && a.GetMajorVersion(a.client) > 0)
            return a.client;
        // Stale — destroy and reconnect
        if (a.DestroyClient) a.DestroyClient(a.client);
        a.client = nullptr;
    }

    a.client = a.ConnectW(nullptr);
    return a.client;
}

// Map our legacy v2 sort IDs to v3 (property_id, ascending) pairs.
struct V3Sort { DWORD property_id; BOOL ascending; };

V3Sort map_sort(int sort) {
    switch (sort) {
        case 1:  return {EVERYTHING3_PROPERTY_ID_NAME,          TRUE};
        case 2:  return {EVERYTHING3_PROPERTY_ID_NAME,          FALSE};
        case 3:  return {EVERYTHING3_PROPERTY_ID_PATH,          TRUE};
        case 4:  return {EVERYTHING3_PROPERTY_ID_PATH,          FALSE};
        case 5:  return {EVERYTHING3_PROPERTY_ID_SIZE,          TRUE};
        case 6:  return {EVERYTHING3_PROPERTY_ID_SIZE,          FALSE};
        case 7:  return {EVERYTHING3_PROPERTY_ID_EXTENSION,     TRUE};
        case 8:  return {EVERYTHING3_PROPERTY_ID_EXTENSION,     FALSE};
        case 9:  return {EVERYTHING3_PROPERTY_ID_TYPE,          TRUE};
        case 10: return {EVERYTHING3_PROPERTY_ID_TYPE,          FALSE};
        case 11: return {EVERYTHING3_PROPERTY_ID_DATE_CREATED,  TRUE};
        case 12: return {EVERYTHING3_PROPERTY_ID_DATE_CREATED,  FALSE};
        case 13: return {EVERYTHING3_PROPERTY_ID_DATE_MODIFIED, TRUE};
        case 14: return {EVERYTHING3_PROPERTY_ID_DATE_MODIFIED, FALSE};
        case 15: return {EVERYTHING3_PROPERTY_ID_ATTRIBUTES,    TRUE};
        case 16: return {EVERYTHING3_PROPERTY_ID_ATTRIBUTES,    FALSE};
        case 23: return {EVERYTHING3_PROPERTY_ID_DATE_ACCESSED, TRUE};
        case 24: return {EVERYTHING3_PROPERTY_ID_DATE_ACCESSED, FALSE};
        default: return {EVERYTHING3_PROPERTY_ID_NAME,          TRUE};
    }
}

// Convert a v3 UINT64 filetime to ISO 8601 string.
// v3 dates are Windows FILETIME (100ns intervals since 1601-01-01) as UINT64.
std::string uint64_to_iso(UINT64 ft) {
    if (ft == 0 || ft == EVERYTHING3_UINT64_MAX) return {};
    FILETIME filetime;
    filetime.dwLowDateTime  = static_cast<DWORD>(ft & 0xFFFFFFFF);
    filetime.dwHighDateTime = static_cast<DWORD>(ft >> 32);
    SYSTEMTIME st;
    FileTimeToSystemTime(&filetime, &st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::string format_attributes(uint32_t attr) {
    std::string result;
    if (attr & FILE_ATTRIBUTE_READONLY)     result += 'R';
    if (attr & FILE_ATTRIBUTE_HIDDEN)       result += 'H';
    if (attr & FILE_ATTRIBUTE_SYSTEM)       result += 'S';
    if (attr & FILE_ATTRIBUTE_DIRECTORY)    result += 'D';
    if (attr & FILE_ATTRIBUTE_ARCHIVE)      result += 'A';
    if (attr & FILE_ATTRIBUTE_COMPRESSED)   result += 'C';
    if (attr & FILE_ATTRIBUTE_ENCRYPTED)    result += 'E';
    return result.empty() ? "N" : result;
}

} // anonymous namespace

namespace everything {

bool is_running() {
    auto& a = api();
    if (!a.loaded || !a.ConnectW) return false;
    return ensure_client() != nullptr;
}

VersionInfo get_version() {
    VersionInfo vi;
    auto& a = api();
    if (!a.loaded || !a.GetMajorVersion) return vi;

    EVERYTHING3_CLIENT* client = ensure_client();
    if (!client) return vi;

    vi.major    = static_cast<int>(a.GetMajorVersion(client));
    vi.minor    = static_cast<int>(a.GetMinorVersion(client));
    vi.revision = static_cast<int>(a.GetRevision(client));
    vi.build    = static_cast<int>(a.GetBuildNumber(client));
    vi.available = (vi.major > 0);
    return vi;
}

int get_result_count(const std::string& query) {
    auto& a = api();
    if (!a.loaded || !a.Search) return -1;

    EVERYTHING3_CLIENT* client = ensure_client();
    if (!client) return -1;

    EVERYTHING3_SEARCH_STATE* ss = a.CreateSearchState();
    if (!ss) return -1;

    a.SetSearchTextW(ss, str_utils::utf8_to_wide(query).c_str());
    a.SetSearchViewportCount(ss, 0);
    a.SetSearchRequestTotalSize(ss, TRUE);

    EVERYTHING3_RESULT_LIST* rl = a.Search(client, ss);
    int count = -1;
    if (rl) {
        count = static_cast<int>(a.GetResultListCount(rl));
        a.DestroyResultList(rl);
    }
    a.DestroySearchState(ss);
    return count;
}

SearchResponse search(const SearchOptions& opts) {
    SearchResponse resp;

    auto& a = api();
    if (!a.loaded || !a.Search) {
        resp.error_code = -1;
        resp.error_message = "Everything3 SDK DLL not loaded. "
                             "Ensure Everything3_x64.dll is available.";
        return resp;
    }

    EVERYTHING3_CLIENT* client = ensure_client();
    if (!client) {
        resp.error_code = static_cast<int>(EVERYTHING3_ERROR_IPC_PIPE_NOT_FOUND);
        resp.error_message = "Everything is not running or pipe not accessible. "
                             "Start Everything.exe first.";
        return resp;
    }

    // Create search state
    EVERYTHING3_SEARCH_STATE* ss = a.CreateSearchState();
    if (!ss) {
        resp.error_code = -1;
        resp.error_message = "Failed to create search state.";
        return resp;
    }

    // Configure search
    a.SetSearchTextW(ss, str_utils::utf8_to_wide(opts.query).c_str());
    if (a.SetSearchMatchPath)       a.SetSearchMatchPath(ss, opts.match_path ? TRUE : FALSE);
    if (a.SetSearchMatchCase)       a.SetSearchMatchCase(ss, opts.match_case ? TRUE : FALSE);
    if (a.SetSearchMatchWholeWords) a.SetSearchMatchWholeWords(ss, opts.match_whole_word ? TRUE : FALSE);
    if (a.SetSearchRegex)           a.SetSearchRegex(ss, opts.regex ? TRUE : FALSE);

    // Viewport paging — server only sends what we request
    if (a.SetSearchViewportOffset) a.SetSearchViewportOffset(ss, static_cast<SIZE_T>(opts.offset));
    if (a.SetSearchViewportCount)  a.SetSearchViewportCount(ss, static_cast<SIZE_T>(opts.max_results));
    if (a.SetSearchRequestTotalSize) a.SetSearchRequestTotalSize(ss, TRUE);

    // Sort — map legacy v2 sort IDs to v3 property+direction
    if (a.ClearSearchSorts) a.ClearSearchSorts(ss);
    V3Sort v3s = map_sort(opts.sort);
    if (a.AddSearchSort) a.AddSearchSort(ss, v3s.property_id, v3s.ascending);

    // Property requests — request only what we need
    if (a.ClearSearchPropertyRequests) a.ClearSearchPropertyRequests(ss);
    if (a.AddSearchPropertyRequest) {
        a.AddSearchPropertyRequest(ss, EVERYTHING3_PROPERTY_ID_NAME);
        a.AddSearchPropertyRequest(ss, EVERYTHING3_PROPERTY_ID_PATH);
        a.AddSearchPropertyRequest(ss, EVERYTHING3_PROPERTY_ID_EXTENSION);
        if (opts.include_size)  a.AddSearchPropertyRequest(ss, EVERYTHING3_PROPERTY_ID_SIZE);
        if (opts.include_dates) {
            a.AddSearchPropertyRequest(ss, EVERYTHING3_PROPERTY_ID_DATE_MODIFIED);
            a.AddSearchPropertyRequest(ss, EVERYTHING3_PROPERTY_ID_DATE_CREATED);
            a.AddSearchPropertyRequest(ss, EVERYTHING3_PROPERTY_ID_DATE_ACCESSED);
        }
        a.AddSearchPropertyRequest(ss, EVERYTHING3_PROPERTY_ID_ATTRIBUTES);
    }

    // Execute search
    EVERYTHING3_RESULT_LIST* rl = a.Search(client, ss);
    if (!rl) {
        DWORD err = a.GetLastError ? a.GetLastError() : 0;
        resp.error_code = static_cast<int>(err);
        switch (err) {
            case EVERYTHING3_ERROR_IPC_PIPE_NOT_FOUND:
                resp.error_message = "Everything is not running. "
                                     "Start Everything.exe first."; break;
            case EVERYTHING3_ERROR_DISCONNECTED:
                resp.error_message = "Disconnected from Everything pipe. "
                                     "It may have been restarted."; break;
            case EVERYTHING3_ERROR_OUT_OF_MEMORY:
                resp.error_message = "Out of memory."; break;
            default:
                resp.error_message = "Everything query failed (error " +
                                     std::to_string(err) + ")"; break;
        }
        a.DestroySearchState(ss);
        return resp;
    }

    // Extract results
    resp.success = true;
    SIZE_T total = a.GetResultListCount ? a.GetResultListCount(rl) : 0;
    SIZE_T viewport = a.GetResultListViewportCount ? a.GetResultListViewportCount(rl) : 0;
    resp.total_results = static_cast<int>(total);
    resp.returned_results = static_cast<int>(viewport);
    resp.offset = opts.offset;

    resp.results.reserve(viewport);

    for (SIZE_T i = 0; i < viewport; ++i) {
        SearchResult r;
        r.index = static_cast<int>(i) + opts.offset;

        if (a.GetResultNameW) {
            constexpr SIZE_T buf_size = 32768;
            std::wstring buf(buf_size, L'\0');
            SIZE_T len = a.GetResultNameW(rl, i, buf.data(), buf_size);
            if (len > 0 && len < buf_size) {
                buf.resize(len);
                r.file_name = str_utils::wide_to_utf8(buf);
            }
        }

        if (a.GetResultPathW) {
            constexpr SIZE_T buf_size = 32768;
            std::wstring buf(buf_size, L'\0');
            SIZE_T len = a.GetResultPathW(rl, i, buf.data(), buf_size);
            if (len > 0 && len < buf_size) {
                buf.resize(len);
                r.path = str_utils::wide_to_utf8(buf);
            }
        }

        if (a.GetResultFullPathNameW) {
            constexpr SIZE_T buf_size = 32768;
            std::wstring buf(buf_size, L'\0');
            SIZE_T len = a.GetResultFullPathNameW(rl, i, buf.data(), buf_size);
            if (len > 0 && len < buf_size) {
                buf.resize(len);
                r.full_path = str_utils::wide_to_utf8(buf);
            }
        }
        // Fallback: construct from path + name if API returned empty
        if (r.full_path.empty() && !r.path.empty() && !r.file_name.empty()) {
            r.full_path = r.path + "\\" + r.file_name;
        }

        if (a.GetResultExtensionW) {
            wchar_t ext[64];
            SIZE_T len = a.GetResultExtensionW(rl, i, ext, 64);
            if (len > 0 && len < 64) {
                r.extension = str_utils::wide_to_utf8(std::wstring(ext, len));
            }
        }

        if (a.IsFolderResult) {
            r.is_folder = a.IsFolderResult(rl, i) != 0;
            r.is_file = !r.is_folder;
        }

        if (opts.include_size && a.GetResultSize) {
            UINT64 sz = a.GetResultSize(rl, i);
            if (sz != EVERYTHING3_UINT64_MAX) {
                r.size = static_cast<int64_t>(sz);
            }
        }

        if (opts.include_dates) {
            if (a.GetResultDateModified) {
                UINT64 dm = a.GetResultDateModified(rl, i);
                r.dates.modified = uint64_to_iso(dm);
            }
            if (a.GetResultDateCreated) {
                UINT64 dc = a.GetResultDateCreated(rl, i);
                r.dates.created = uint64_to_iso(dc);
            }
            if (a.GetResultDateAccessed) {
                UINT64 da = a.GetResultDateAccessed(rl, i);
                r.dates.accessed = uint64_to_iso(da);
            }
        }

        if (a.GetResultAttributes) {
            r.attributes = a.GetResultAttributes(rl, i);
        }

        resp.results.push_back(std::move(r));
    }

    a.DestroyResultList(rl);
    a.DestroySearchState(ss);
    return resp;
}

} // namespace everything
