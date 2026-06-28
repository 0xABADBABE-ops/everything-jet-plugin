//
// test_mcp_server.cpp — Tests for the MCP protocol server.
//

#include "test_framework.hpp"
#include "../src/json.hpp"
#include "../src/mcp_server.hpp"

using json::Value;
using mcp::McpServer;
using mcp::ServerInfo;

// Helper: create a server with Everything tools registered (uninitialized)
static McpServer make_test_server() {
    McpServer server(ServerInfo{"test-server", "0.1.0"});
    mcp::register_everything_tools(server);
    return server;
}

// Helper: build a JSON-RPC request
static std::string rpc_request(int id, const std::string& method,
                               const std::string& params_json = "{}") {
    return R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
           R"(,"method":")" + method + R"(","params":)" + params_json + "}";
}

// Helper: create a server and complete the initialize handshake
static McpServer make_initialized_server() {
    auto server = make_test_server();
    server.process_line(rpc_request(0, "initialize",
        R"({"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}})"));
    return server;
}

TEST(mcp_initialize_returns_protocol_version) {
    auto server = make_test_server();
    std::string req = rpc_request(1, "initialize",
        R"({"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}})"
    );

    std::string resp = server.process_line(req);
    CHECK(!resp.empty());

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    CHECK(v["result"].has("protocolVersion"));
    CHECK(!v["result"].at("protocolVersion").as_string().empty());
}

TEST(mcp_initialize_returns_server_info) {
    auto server = make_test_server();
    std::string req = rpc_request(1, "initialize");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v["result"].has("serverInfo"));
    CHECK_STR_EQ(v["result"]["serverInfo"].at("name").as_string(), "test-server");
}

TEST(mcp_initialize_returns_capabilities) {
    auto server = make_test_server();
    std::string req = rpc_request(1, "initialize");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v["result"].has("capabilities"));
    CHECK(v["result"]["capabilities"].has("tools"));
}

TEST(mcp_initialized_notification_no_response) {
    auto server = make_test_server();
    std::string req = R"({"jsonrpc":"2.0","method":"notifications/initialized"})";

    std::string resp = server.process_line(req);
    CHECK(resp.empty()); // Notifications should not produce a response
}

TEST(mcp_tools_list_returns_tools) {
    auto server = make_initialized_server();
    std::string req = rpc_request(2, "tools/list");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v.has("result"));
    CHECK(v["result"].has("tools"));
    CHECK(v["result"]["tools"].is_array());
    CHECK(v["result"]["tools"].size() >= 4); // search, get_count, is_running, get_version
}

TEST(mcp_tools_list_has_search_tool) {
    auto server = make_initialized_server();
    std::string req = rpc_request(2, "tools/list");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    const auto& tools = v["result"]["tools"].as_array();
    bool found_search = false;
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "search") {
            found_search = true;
            CHECK(t.has("description"));
            CHECK(t.has("inputSchema"));
            CHECK(t["inputSchema"].has("properties"));
            CHECK(t["inputSchema"]["properties"].has("query"));
            break;
        }
    }
    CHECK(found_search);
}

TEST(mcp_search_tool_has_required_query) {
    auto server = make_initialized_server();
    std::string req = rpc_request(2, "tools/list");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    const auto& tools = v["result"]["tools"].as_array();
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "search") {
            const auto& required = t["inputSchema"].at("required").as_array();
            bool has_query = false;
            for (const auto& r : required) {
                if (r.as_string() == "query") has_query = true;
            }
            CHECK(has_query);
            break;
        }
    }
}

TEST(mcp_ping_returns_empty_result) {
    auto server = make_initialized_server();
    std::string req = rpc_request(3, "ping");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v.has("result"));
    CHECK(v["result"].is_object());
}

TEST(mcp_unknown_method_returns_error) {
    auto server = make_initialized_server();
    std::string req = rpc_request(4, "nonexistent/method");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v.has("error"));
    CHECK_EQ(v["error"].at("code").as_int(), -32601);
}

TEST(mcp_malformed_json_returns_parse_error) {
    auto server = make_test_server();
    std::string req = "{invalid json}";

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v.has("error"));
    CHECK_EQ(v["error"].at("code").as_int(), -32700);
}

TEST(mcp_call_unknown_tool_returns_error) {
    auto server = make_initialized_server();
    std::string req = rpc_request(5, "tools/call",
        R"({"name":"nonexistent_tool","arguments":{}})");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v.has("error"));
}

TEST(mcp_call_search_returns_content) {
    auto server = make_initialized_server();
    std::string req = rpc_request(6, "tools/call",
        R"({"name":"search","arguments":{"query":"*.txt","max_results":5}})");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v.has("result"));
    CHECK(v["result"].has("content"));
    CHECK(v["result"]["content"].is_array());
    CHECK(v["result"]["content"].size() > 0);
    CHECK_STR_EQ(v["result"]["content"][0].at("type").as_string(), "text");
}

TEST(mcp_call_is_running_returns_content) {
    auto server = make_initialized_server();
    std::string req = rpc_request(7, "tools/call",
        R"({"name":"is_running","arguments":{}})");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v.has("result"));
    CHECK(v["result"].has("content"));
}

TEST(mcp_call_get_version_returns_content) {
    auto server = make_initialized_server();
    std::string req = rpc_request(8, "tools/call",
        R"({"name":"get_version","arguments":{}})");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v.has("result"));
    CHECK(v["result"].has("content"));

    // Parse the inner text content
    std::string text = v["result"]["content"][0].at("text").as_string();
    auto inner = Value::parse(text);
    CHECK(inner.has("available"));
}

TEST(mcp_call_get_count_returns_content) {
    auto server = make_initialized_server();
    std::string req = rpc_request(9, "tools/call",
        R"({"name":"get_count","arguments":{"query":"*.exe"}})");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v.has("result"));
    CHECK(v["result"].has("content"));
}

TEST(mcp_batch_request) {
    auto server = make_initialized_server();
    std::string batch = "[";
    batch += rpc_request(1, "ping");
    batch += ",";
    batch += rpc_request(2, "tools/list");
    batch += "]";

    std::string resp = server.process_line(batch);
    CHECK(!resp.empty());

    auto v = Value::parse(resp);
    CHECK(v.is_array());
    CHECK_EQ(v.size(), 2u);
}

TEST(mcp_id_is_echoed_back) {
    auto server = make_initialized_server();
    std::string req = rpc_request(999, "ping");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK_EQ(v.at("id").as_int(), 999);
}

TEST(mcp_string_id_supported) {
    auto server = make_initialized_server();
    std::string req = R"({"jsonrpc":"2.0","id":"abc123","method":"ping"})";

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK_STR_EQ(v.at("id").as_string(), "abc123");
}

TEST(mcp_response_has_jsonrpc_field) {
    auto server = make_initialized_server();
    std::string req = rpc_request(1, "ping");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v.has("jsonrpc"));
    CHECK_STR_EQ(v.at("jsonrpc").as_string(), "2.0");
}

TEST(mcp_tools_list_schema_has_sort_enum) {
    auto server = make_initialized_server();
    std::string req = rpc_request(2, "tools/list");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    const auto& tools = v["result"]["tools"].as_array();
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "search") {
            const auto& props = t["inputSchema"]["properties"].as_object();
            CHECK(props.find("sort") != props.end());
            const auto& sort_prop = props.at("sort");
            CHECK(sort_prop.has("enum"));
            CHECK(sort_prop.at("enum").is_array());
            CHECK(sort_prop.at("enum").size() > 0);
            break;
        }
    }
}

TEST(mcp_rejects_tools_list_before_init) {
    auto server = make_test_server();
    std::string resp = server.process_line(rpc_request(1, "tools/list"));

    auto v = Value::parse(resp);
    CHECK(v.has("error"));
    CHECK_EQ(v["error"].at("code").as_int(), -32002);
}

TEST(mcp_rejects_ping_before_init) {
    auto server = make_test_server();
    std::string resp = server.process_line(rpc_request(1, "ping"));

    auto v = Value::parse(resp);
    CHECK(v.has("error"));
    CHECK_EQ(v["error"].at("code").as_int(), -32002);
}

TEST(mcp_rejects_tools_call_before_init) {
    auto server = make_test_server();
    std::string resp = server.process_line(rpc_request(1, "tools/call",
        R"({"name":"search","arguments":{"query":"*"}})"));

    auto v = Value::parse(resp);
    CHECK(v.has("error"));
    CHECK_EQ(v["error"].at("code").as_int(), -32002);
}

TEST(mcp_allows_requests_after_init) {
    auto server = make_initialized_server();

    // Should work now
    std::string resp = server.process_line(rpc_request(1, "ping"));
    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    CHECK(v["result"].is_object());
}

// ============================================================
// New tool registration tests
// ============================================================

TEST(mcp_tools_list_has_13_tools) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/list"));

    auto v = Value::parse(resp);
    CHECK(v["result"]["tools"].is_array());
    CHECK_EQ(v["result"]["tools"].size(), 13u);
}

TEST(mcp_tools_list_has_search_content) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/list"));

    auto v = Value::parse(resp);
    const auto& tools = v["result"]["tools"].as_array();
    bool found = false;
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "search_content") {
            found = true;
            CHECK(t.has("description"));
            CHECK(t.has("inputSchema"));
            CHECK(t["inputSchema"]["properties"].has("pattern"));
            CHECK(t["inputSchema"].has("required"));
            break;
        }
    }
    CHECK(found);
}

TEST(mcp_tools_list_has_recently_changed) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/list"));

    auto v = Value::parse(resp);
    const auto& tools = v["result"]["tools"].as_array();
    bool found = false;
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "recently_changed") {
            found = true;
            CHECK(t["inputSchema"]["properties"].has("since"));
            break;
        }
    }
    CHECK(found);
}

TEST(mcp_tools_list_has_largest_files) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/list"));

    auto v = Value::parse(resp);
    const auto& tools = v["result"]["tools"].as_array();
    bool found = false;
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "largest_files") {
            found = true;
            CHECK(t["inputSchema"]["properties"].has("top"));
            break;
        }
    }
    CHECK(found);
}

TEST(mcp_tools_list_has_duplicates) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/list"));

    auto v = Value::parse(resp);
    const auto& tools = v["result"]["tools"].as_array();
    bool found = false;
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "duplicates") {
            found = true;
            CHECK(t["inputSchema"]["properties"].has("min_size"));
            break;
        }
    }
    CHECK(found);
}

TEST(mcp_search_content_returns_content) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/call",
        R"({"name":"search_content","arguments":{"pattern":"Everything","max_results":5}})"));

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    CHECK(v["result"].has("content"));
    CHECK(v["result"]["content"].is_array());

    // Parse inner JSON
    std::string text = v["result"]["content"][0].at("text").as_string();
    auto inner = Value::parse(text);
    CHECK(inner.has("files_searched"));
    CHECK(inner.has("total_matches"));
    CHECK(inner.has("matches"));
}

TEST(mcp_recently_changed_returns_content) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/call",
        R"({"name":"recently_changed","arguments":{"since":"7d","max_results":5}})"));

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    CHECK(v["result"].has("content"));
}

TEST(mcp_largest_files_returns_content) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/call",
        R"({"name":"largest_files","arguments":{"top":3}})"));

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    CHECK(v["result"].has("content"));
}

TEST(mcp_duplicates_returns_content) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/call",
        R"({"name":"duplicates","arguments":{"max_results":10}})"));

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    CHECK(v["result"].has("content"));
}

// ============================================================
// New Phase 1 tool tests
// ============================================================

TEST(mcp_tools_list_has_smart_search) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/list"));

    auto v = Value::parse(resp);
    const auto& tools = v["result"]["tools"].as_array();
    bool found = false;
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "smart_search") {
            found = true;
            CHECK(t["inputSchema"]["properties"].has("intent"));
            CHECK(t["inputSchema"].has("required"));
            break;
        }
    }
    CHECK(found);
}

TEST(mcp_tools_list_has_open) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/list"));

    auto v = Value::parse(resp);
    const auto& tools = v["result"]["tools"].as_array();
    bool found = false;
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "open") {
            found = true;
            CHECK(t["inputSchema"]["properties"].has("path"));
            break;
        }
    }
    CHECK(found);
}

TEST(mcp_tools_list_has_reveal) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/list"));

    auto v = Value::parse(resp);
    const auto& tools = v["result"]["tools"].as_array();
    bool found = false;
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "reveal") {
            found = true;
            CHECK(t["inputSchema"]["properties"].has("path"));
            break;
        }
    }
    CHECK(found);
}

TEST(mcp_tools_list_has_copy_path) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/list"));

    auto v = Value::parse(resp);
    const auto& tools = v["result"]["tools"].as_array();
    bool found = false;
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "copy_path") {
            found = true;
            CHECK(t["inputSchema"]["properties"].has("path"));
            break;
        }
    }
    CHECK(found);
}

TEST(mcp_tools_list_has_preview) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/list"));

    auto v = Value::parse(resp);
    const auto& tools = v["result"]["tools"].as_array();
    bool found = false;
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "preview") {
            found = true;
            CHECK(t["inputSchema"]["properties"].has("path"));
            CHECK(t["inputSchema"]["properties"].has("max_lines"));
            break;
        }
    }
    CHECK(found);
}

TEST(mcp_tools_list_duplicates_has_method) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/list"));

    auto v = Value::parse(resp);
    const auto& tools = v["result"]["tools"].as_array();
    bool found = false;
    for (const auto& t : tools) {
        if (t.at("name").as_string() == "duplicates") {
            found = true;
            CHECK(t["inputSchema"]["properties"].has("method"));
            break;
        }
    }
    CHECK(found);
}

TEST(mcp_smart_search_returns_query) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/call",
        R"({"name":"smart_search","arguments":{"intent":"large pdfs from last month"}})"));

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    CHECK(v["result"].has("content"));

    std::string text = v["result"]["content"][0].at("text").as_string();
    auto inner = Value::parse(text);
    CHECK(inner.has("query"));
    CHECK(inner.has("intent"));
    std::string query = inner.at("query").as_string();
    CHECK(query.find("ext:pdf") != std::string::npos);
    CHECK(query.find("size:>10mb") != std::string::npos);
    CHECK(query.find("last30days") != std::string::npos);
}

TEST(mcp_open_nonexistent_returns_error) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/call",
        R"({"name":"open","arguments":{"path":"Z:\\nonexistent\\file.xyz"}})"));

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    std::string text = v["result"]["content"][0].at("text").as_string();
    auto inner = Value::parse(text);
    CHECK_EQ(inner.at("success").as_bool(), false);
}

TEST(mcp_copy_path_nonexistent_returns_error) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/call",
        R"({"name":"copy_path","arguments":{"path":"Z:\\nonexistent\\file.xyz"}})"));

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    std::string text = v["result"]["content"][0].at("text").as_string();
    auto inner = Value::parse(text);
    CHECK_EQ(inner.at("success").as_bool(), false);
}

TEST(mcp_preview_nonexistent_returns_error) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "tools/call",
        R"({"name":"preview","arguments":{"path":"Z:\\nonexistent\\file.xyz"}})"));

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    // Preview errors use isError=true in content format
    CHECK(v["result"].has("content"));
}

// ============================================================
// MCP Resources tests
// ============================================================

TEST(mcp_initialize_advertises_resources) {
    auto server = make_test_server();
    std::string req = rpc_request(1, "initialize");

    std::string resp = server.process_line(req);
    auto v = Value::parse(resp);

    CHECK(v["result"]["capabilities"].has("resources"));
}

TEST(mcp_resources_list_returns_static_resources) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "resources/list"));

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    CHECK(v["result"].has("resources"));
    CHECK(v["result"]["resources"].is_array());
    CHECK(v["result"]["resources"].size() >= 3); // dupe:, count/, ext:pdf
}

TEST(mcp_resources_templates_list_returns_templates) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "resources/templates/list"));

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    CHECK(v["result"].has("resourceTemplates"));
    CHECK(v["result"]["resourceTemplates"].is_array());
    CHECK(v["result"]["resourceTemplates"].size() >= 2); // search/{query}, count/{query}

    // Verify template has uriTemplate field
    bool found_search = false;
    for (const auto& tmpl : v["result"]["resourceTemplates"].as_array()) {
        if (tmpl.at("uriTemplate").as_string().find("search") != std::string::npos) {
            found_search = true;
            break;
        }
    }
    CHECK(found_search);
}

TEST(mcp_resources_read_search_template) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "resources/read",
        R"({"uri":"everything://search/ext:exe"})"));

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    CHECK(v["result"].has("contents"));
    CHECK(v["result"]["contents"].is_array());
    CHECK(v["result"]["contents"].size() > 0);

    // Verify the content has a text field
    CHECK(v["result"]["contents"][0].has("text"));
    std::string text = v["result"]["contents"][0].at("text").as_string();
    auto inner = Value::parse(text);
    CHECK(inner.has("query"));
    CHECK(inner.has("results"));
}

TEST(mcp_resources_read_unknown_uri_fails) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "resources/read",
        R"({"uri":"unknown://something"})"));

    auto v = Value::parse(resp);
    CHECK(v.has("error"));
}

TEST(mcp_resources_read_static_resource) {
    auto server = make_initialized_server();
    std::string resp = server.process_line(rpc_request(1, "resources/read",
        R"({"uri":"everything://count/"})"));

    auto v = Value::parse(resp);
    CHECK(v.has("result"));
    CHECK(v["result"].has("contents"));

    std::string text = v["result"]["contents"][0].at("text").as_string();
    auto inner = Value::parse(text);
    CHECK(inner.has("available"));
}

TEST(mcp_rejects_resources_before_init) {
    auto server = make_test_server();
    std::string resp = server.process_line(rpc_request(1, "resources/list"));

    auto v = Value::parse(resp);
    CHECK(v.has("error"));
    CHECK_EQ(v["error"].at("code").as_int(), -32002);
}
