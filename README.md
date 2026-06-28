<div align="center">

# ⚡ everything-jet-plugin

Native C++23 MCP server for exposing [Everything](https://www.voidtools.com) 1.5 search to AI agents.

[![CI](https://img.shields.io/github/actions/workflow/status/0xabadbabe-ops/everything-jet-plugin/ci.yml?branch=main&style=flat-square&label=ci&logo=github)](https://github.com/0xabadbabe-ops/everything-jet-plugin/actions/workflows/ci.yml)
![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=flat-square&logo=cplusplus)
![Windows](https://img.shields.io/badge/platform-Windows-0078D4?style=flat-square&logo=windows)
![MCP](https://img.shields.io/badge/protocol-MCP-111827?style=flat-square)
![Tests](https://img.shields.io/badge/tests-182%20passing-2ea043?style=flat-square)

**13 tools** · **MCP resources** · **Everything3 SDK v3** · **Zero runtime dependencies**

</div>

---

## Overview

`everything-mcp` bridges the **voidtools Everything** file indexer (1.5) to AI assistants via the [Model Context Protocol](https://modelcontextprotocol.io) (MCP) over stdio. It lets your AI agent search, inspect, and interact with every file on your system — instantly.

No Node.js. No Python. No runtime dependencies. Just a single native executable that loads `Everything3_x64.dll` and speaks JSON-RPC.

```
┌────────────┐     stdio      ┌─────────────────────┐    IPC3 named pipe   ┌──────────────┐
│  AI Agent  │◄══════════════►│    everything-mcp   │◄═══════════════════► │  Everything  │
│ (Qwen etc) │    JSON-RPC    │    (native C++23)   │   Everything3 SDK    │   1.5 index  │
└────────────┘                └─────────────────────┘                      └──────────────┘
```

> **SDK:** This project targets the **Everything3 SDK v3** (`Everything3_x64.dll`, named-pipe IPC3), not the legacy `Everything64.dll` SDK. Point CMake at the SDK with `EVERYTHING_SDK_DIR`, or keep it as a sibling folder at `../SDK-3.0.0.9`.

## Features

### 🔍 Search Tools (5)

| Tool | Description |
|------|-------------|
| `search` | Full Everything search with wildcards, boolean operators, regex, pagination, and 14 sort modes |
| `get_count` | Get result count without fetching file paths — quick existence checks |
| `search_content` | Search **inside** file contents with regex/snippet extraction and context lines |
| `recently_changed` | Find files modified within a time window (`5m`, `1h`, `3d`, `2w`, or ISO date) |
| `largest_files` | Find the largest files by size, scoped by path or extension |

### 🧠 Smart Query Builder

| Tool | Description |
|------|-------------|
| `smart_search` | Natural language → Everything syntax. *"large pdfs from last month"* → `ext:pdf dm:last30days size:>10mb` |

Recognizes file types (pdfs, images, videos, code, archives), size qualifiers (large, huge, 10mb), time filters (today, last week, recent), path scopes, and exclusions — all from plain English.

### ⚡ Action Tools

| Tool | Description |
|------|-------------|
| `open` | Open a file with its default application (`ShellExecute`) |
| `reveal` | Select a file in Windows Explorer (`explorer /select`) |
| `copy_path` | Copy absolute path to clipboard (`CF_UNICODETEXT`) |

### 📄 File Preview

| Tool | Description |
|------|-------------|
| `preview` | Type-aware metadata extraction in a single call |

| File Type | Extracts |
|-----------|----------|
| **Text/Code** | First N lines, total line count, truncation flag |
| **PE Binaries** (exe/dll/sys) | Version, company name, product name, description, copyright |
| **Images** (png/jpg/gif/bmp/webp) | Dimensions (width × height), format, bpp — raw header parsing, no GDI+ |
| **Archives** (zip) | Central directory listing — filenames, compressed/uncompressed sizes |
| **Other** | File size, dates, attributes |

### 🔄 Duplicate Finder

| Tool | Description |
|------|-------------|
| `duplicates` | Find duplicate files with two methods |

| Method | How it works |
|--------|-------------|
| `name` *(default)* | Everything's `dupe:` operator — fast grouping by filename |
| `hash` | Content-accurate: `dupe:size` pre-filter → group by size → incremental **CNG MD5** hashing (1 MB streaming chunks) → group by hash. Reports wasted space. |

### 📡 MCP Resources

Standard search results exposed as native MCP resource URIs:

| URI | Returns |
|-----|---------|
| `everything://search/{query}` | JSON search results |
| `everything://count/{query}` | Result count |
| `everything://search/dupe:` | Static: duplicate files overview |
| `everything://count/` | Static: Everything availability |
| `everything://search/ext:pdf` | Static: all PDFs overview |

## Quick Start

### Prerequisites

- **Windows 10/11 (x64)**
- **Everything 1.5** running (https://www.voidtools.com)
- **Everything3 SDK v3.0.0.9+** (download from voidtools, extract locally)
- **Visual Studio 2022** (MSVC 14.50+) with C++ CMake tools
- **CMake 3.20+**

### Build

The simplest layout is:

```text
everything-ai/
├── SDK-3.0.0.9/
└── everything-jet-plugin/
```

From `everything-jet-plugin/`:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\everything-mcp-tests.exe
```

If the SDK is somewhere else:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DEVERYTHING_SDK_DIR="C:\path\to\SDK-3.0.0.9"
cmake --build build --config Release
```

The executable and `Everything3_x64.dll` are placed in `build\Release\`.

### Everything setup (pipe de-elevation)

Everything must run **non-elevated** so the IPC3 named pipe is accessible to non-admin processes like MCP servers. The recommended setup:

1. **Enable the Everything Service** (handles indexing as SYSTEM):
   ```powershell
   sudo pwsh -Command "Set-Service -Name 'Everything' -StartupType Automatic; Start-Service -Name 'Everything'"
   ```

2. **Set `run_as_admin=0`** in `Everything.ini` (stop Everything before editing)

3. **Start Everything.exe** — it connects to the service and creates a user-accessible pipe

If you get `ERROR_ACCESS_DENIED` connecting to the pipe, an elevated Everything process is holding it — kill it by PID (elevated) and restart non-elevated.

### Register with your MCP client

Add to your MCP client configuration (e.g., Qwen Code's `mcp_servers.json`):

```json
{
  "mcpServers": {
    "everything-mcp": {
      "command": "C:\\path\\to\\build\\Release\\everything-mcp.exe"
    }
  }
}
```

## Usage

### Ask your AI agent

```
"Find all PDF files larger than 50MB on my D: drive"
→ smart_search: intent="large pdfs on D:\" → ext:pdf size:>10mb path:"D:\"

"What version of Chrome do I have installed?"
→ preview: path="C:\Program Files\Google\Chrome\Application\chrome.exe"
→ pe_metadata: { file_version: "137.0.7151.69", company_name: "Google LLC" }

"Show me what changed in my project in the last 30 minutes"
→ recently_changed: since="30m", path="C:\Projects\myapp"

"Find duplicate files in my Downloads and tell me how much space I'm wasting"
→ duplicates: method="hash", path="C:\Users\me\Downloads"
→ { wasted_space: 8589934592, duplicate_groups: 47 }
```

### Command Line

```powershell
# Run as stdio MCP server
build\Release\everything-mcp.exe

# Show help
build\Release\everything-mcp.exe --help

# Version
build\Release\everything-mcp.exe --version
```

## Architecture

```
everything-jet-plugin/
├── src/
│   ├── main.cpp                 # Entry point, --help, stdio loop
│   ├── mcp_server.hpp/cpp       # MCP JSON-RPC server, resource handlers, tool dispatch
│   ├── everything_wrapper.hpp/cpp  # Everything3 SDK v3 wrapper (LoadLibrary/GetProcAddress, IPC3 pipe client)
│   ├── content_search.hpp/cpp   # Content search with snippet extraction
│   ├── smart_query.hpp/cpp      # Natural language → Everything syntax translator
│   ├── actions.hpp/cpp          # ShellExecute / clipboard action tools
│   ├── file_hashing.hpp/cpp     # CNG BCrypt MD5 incremental hashing
│   ├── file_preview.hpp/cpp     # Type-aware metadata: PE, image, ZIP, text
│   ├── json.hpp                 # Dependency-free JSON parser + serializer
│   └── string_utils.hpp         # UTF-8 ↔ UTF-16 conversion
├── tests/
│   ├── test_main.cpp            # Test runner entry point
│   ├── test_json.cpp            # JSON parser/serializer tests
│   ├── test_string_utils.cpp    # String conversion tests
│   ├── test_everything.cpp      # Live Everything3 SDK integration tests
│   ├── test_content_search.cpp  # Content search tests
│   ├── test_smart_query.cpp     # NL query builder tests
│   ├── test_actions.cpp         # Action tool tests (clipboard round-trip)
│   ├── test_file_hashing.cpp    # MD5 known-vector + streaming tests
│   ├── test_file_preview.cpp    # Image/PE/ZIP parsing tests
│   └── test_mcp_server.cpp      # MCP protocol + resource tests
├── CMakeLists.txt
└── README.md
```

### Design Principles

- **Zero external dependencies** — custom JSON parser, custom test framework, no npm/pip/vcpkg
- **Everything3 SDK v3** — named-pipe IPC3 (`Everything3_x64.dll`), client-handle architecture with per-search state objects, server-side viewport paging
- **SDK loaded dynamically** — `LoadLibrary`/`GetProcAddress`, auto-reconnect on pipe disconnect
- **Long-path safe** — all path buffers use heap allocation (32K chars), never `MAX_PATH`
- **Thread-safe** — `std::atomic<bool>` flags, `std::mutex` for client connection guard

## Testing

```powershell
# Run all 182 tests against live Everything
build\Release\everything-mcp-tests.exe
```

```
Running 182 test(s)...
  [PASS] md5_abc
  [PASS] md5_file_large_streaming
  [PASS] smart_query_complex_intent
  [PASS] preview_png_dimensions
  [PASS] preview_zip_three_entries
  [PASS] actions_copy_path_roundtrip
  [PASS] mcp_resources_read_search_template
  ...
================================================
Results: 182 passed, 0 failed (of 182 total)
```

Test coverage includes:
- **MD5 hashing** — verified against RFC 1321 known test vectors + 2.5 MB streaming
- **Image parsing** — fixture PNG/GIF/BMP with known dimensions
- **ZIP listing** — synthesized central directory with known entries
- **NL query builder** — file types, sizes, dates, paths, exclusions
- **Clipboard** — `copy_path` round-trip verified via `GetClipboardData`
- **MCP protocol** — init gate, batch requests, resources, error handling
- **Live SDK integration** — search, pagination, sort, count, version, empty queries

## Requirements

| Component | Version |
|-----------|---------|
| Everything | 1.5.0.1415+ |
| Everything3 SDK | v3.0.0.9+ |
| Windows | 10/11 (x64) |
| Compiler | MSVC 14.50+ (`/std:c++latest`) |
| CMake | 3.20+ |

## Tool Reference

| # | Tool | Category | Requires Everything |
|---|------|----------|-------------------|
| 1 | `search` | Search | ✅ |
| 2 | `get_count` | Search | ✅ |
| 3 | `is_running` | Diagnostics | ✅ |
| 4 | `get_version` | Diagnostics | ✅ |
| 5 | `search_content` | Search | ✅ |
| 6 | `recently_changed` | Search | ✅ |
| 7 | `largest_files` | Search | ✅ |
| 8 | `duplicates` | Dedup | ✅ |
| 9 | `smart_search` | Smart Query | ✅ (only if `execute=true`) |
| 10 | `open` | Action | ❌ |
| 11 | `reveal` | Action | ❌ |
| 12 | `copy_path` | Action | ❌ |
| 13 | `preview` | File Insight | ❌ |

## Roadmap

### Phase 2 (Planned)
- **Metadata tool** — expose Everything's 418-property system (hashes, EXIF, media tags, PE info) with adapter fallback to C++ parsing
- **Journal watch** — `Everything3_ReadJournal` change stream (USN-based, callback-driven) replacing `ReadDirectoryChangesW`
- **Search sessions** — stateful result sets with `refine_search`
- **HTTP/ETP transport** — query remote Everything instances via built-in HTTP server

## CI Badge

The README uses this GitHub Actions status badge:

```markdown
[![CI](https://img.shields.io/github/actions/workflow/status/0xabadbabe-ops/everything-jet-plugin/ci.yml?branch=main&style=flat-square&label=ci&logo=github)](https://github.com/0xabadbabe-ops/everything-jet-plugin/actions/workflows/ci.yml)
```

## License

This repository does not currently include a standalone source license file.
The Everything3 SDK remains subject to the licensing terms that ship with the SDK package you install.
If you plan to publish this project publicly on GitHub, add a `LICENSE` file that matches how you want to distribute the source.

---

<p align="center">Built with ⚡ C++23 · MCP · IPC3 named pipes</p>
