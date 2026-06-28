#pragma once

//
// mcp_server.hpp — MCP (Model Context Protocol) server over stdio.
// Implements JSON-RPC 2.0 with tool registration, resource templates, and dispatch.
//

#include "json.hpp"
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mcp {

using ToolHandler = std::function<json::Value(const json::Value& args)>;

struct ToolDef {
    std::string name;
    std::string description;
    json::Value input_schema;
    ToolHandler handler;
};

// Handler for resource reads — receives the full URI, returns JSON content
using ResourceHandler = std::function<json::Value(const std::string& uri)>;

struct StaticResource {
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type = "application/json";
    ResourceHandler handler;
};

struct ResourceTemplateDef {
    std::string uri_template;   // e.g., "everything://search/{query}"
    std::string name;
    std::string description;
    std::string mime_type = "application/json";
    ResourceHandler handler;
};

struct ServerInfo {
    std::string name = "everything-mcp";
    std::string version = "2.0.0";
};

class McpServer {
public:
    explicit McpServer(ServerInfo info = {});

    void register_tool(ToolDef def);
    void register_resource_template(ResourceTemplateDef def);
    void register_static_resource(StaticResource res);

    // Process a single JSON-RPC message and return the response (or null for notifications)
    std::optional<json::Value> handle_message(const json::Value& message);

    // Run the stdio loop: read lines from stdin, process, write to stdout
    void run();

    // For testing: process raw JSON text and return response text (empty for notifications)
    std::string process_line(const std::string& line);

    // Get tool list metadata (for tools/list response)
    json::Value build_tools_list() const;

    // Check if server has been initialized
    bool is_initialized() const { return initialized_; }

private:
    ServerInfo info_;
    std::vector<ToolDef> tools_;
    std::unordered_map<std::string, size_t> tool_index_;
    std::vector<ResourceTemplateDef> resource_templates_;
    std::vector<StaticResource> static_resources_;
    bool initialized_ = false;

    json::Value make_response(const json::Value& id, json::Value result);
    json::Value make_error(const json::Value& id, int code, const std::string& message,
                           json::Value data = nullptr);

    json::Value handle_initialize(const json::Value& params);
    json::Value handle_tools_list();
    json::Value handle_tools_call(const json::Value& params);
    json::Value handle_ping();
    json::Value handle_resources_list();
    json::Value handle_resources_templates_list();
    json::Value handle_resources_read(const json::Value& params);
};

// Register all Everything SDK tools with the server
void register_everything_tools(McpServer& server);

} // namespace mcp
