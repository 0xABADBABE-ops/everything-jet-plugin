//
// main.cpp — Everything MCP Server entry point.
// Runs an MCP server over stdio that exposes Everything file search to AI agents.
//

#include "mcp_server.hpp"

#include <cstdio>
#include <string>

int main(int argc, char* argv[]) {
    // Check for --help or -h
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::fprintf(stderr,
                "Everything MCP Server\n"
                "\n"
                "Exposes voidtools Everything instant file search to AI agents\n"
                "via the Model Context Protocol (MCP) over stdio.\n"
                "\n"
                "Usage:\n"
                "  everything-mcp               Run as stdio MCP server\n"
                "  everything-mcp --version     Print version and exit\n"
                "  everything-mcp --help        Show this help\n"
                "\n"
                "Requires Everything (https://www.voidtools.com) to be running.\n"
                "Everything3_x64.dll must be in PATH or alongside this executable.\n"
                "\n"
                "Tools exposed (13):\n"
                "  search            Search files/folders with Everything syntax\n"
                "  get_count         Get result count without fetching results\n"
                "  is_running        Check if Everything is available\n"
                "  get_version       Get Everything version info\n"
                "  search_content    Search inside file contents with snippet extraction\n"
                "  recently_changed  Find files modified within a time window\n"
                "  largest_files     Find largest files by size\n"
                "  duplicates        Find duplicate files (name or MD5 hash mode)\n"
                "  smart_search      Natural language to Everything query builder\n"
                "  open              Open a file with its default application\n"
                "  reveal            Reveal a file in Windows Explorer\n"
                "  copy_path         Copy file path to clipboard\n"
                "  preview           Preview file with type-aware metadata extraction\n"
                "\n"
                "Resources:\n"
                "  everything://search/{query}  Search results as JSON\n"
                "  everything://count/{query}   Result count as JSON\n"
            );
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::printf("everything-mcp 2.0.0\n");
            return 0;
        }
    }

    mcp::ServerInfo info;
    info.name = "everything-mcp";
    info.version = "2.0.0";

    mcp::McpServer server(info);
    mcp::register_everything_tools(server);

    server.run();

    return 0;
}
