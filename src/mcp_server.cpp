//
// mcp_server.cpp — MCP server implementation (JSON-RPC 2.0 over stdio).
//

#include "mcp_server.hpp"
#include "actions.hpp"
#include "content_search.hpp"
#include "everything_wrapper.hpp"
#include "file_hashing.hpp"
#include "file_preview.hpp"
#include "smart_query.hpp"
#include "string_utils.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>

// MCP protocol version
static constexpr const char* MCP_PROTOCOL_VERSION = "2025-06-18";

namespace mcp {

McpServer::McpServer(ServerInfo info) : info_(std::move(info)) {}

void McpServer::register_tool(ToolDef def) {
    std::string name = def.name;
    tool_index_[name] = tools_.size();
    tools_.push_back(std::move(def));
}

void McpServer::register_resource_template(ResourceTemplateDef def) {
    resource_templates_.push_back(std::move(def));
}

void McpServer::register_static_resource(StaticResource res) {
    static_resources_.push_back(std::move(res));
}

json::Value McpServer::make_response(const json::Value& id, json::Value result) {
    json::Value resp = json::Value::make_object();
    resp["jsonrpc"] = "2.0";
    resp["id"] = id;
    resp["result"] = std::move(result);
    return resp;
}

json::Value McpServer::make_error(const json::Value& id, int code,
                                  const std::string& message, json::Value data) {
    json::Value err = json::Value::make_object();
    err["code"] = code;
    err["message"] = message;
    if (!data.is_null()) err["data"] = std::move(data);

    json::Value resp = json::Value::make_object();
    resp["jsonrpc"] = "2.0";
    resp["id"] = id;
    resp["error"] = std::move(err);
    return resp;
}

json::Value McpServer::handle_initialize(const json::Value& params) {
    json::Value result = json::Value::make_object();
    result["protocolVersion"] = MCP_PROTOCOL_VERSION;

    json::Value caps = json::Value::make_object();
    json::Value tool_caps = json::Value::make_object();
    tool_caps["listChanged"] = false;
    caps["tools"] = std::move(tool_caps);

    json::Value res_caps = json::Value::make_object();
    res_caps["listChanged"] = false;
    res_caps["subscribe"] = false;
    caps["resources"] = std::move(res_caps);

    result["capabilities"] = std::move(caps);

    json::Value server_info = json::Value::make_object();
    server_info["name"] = info_.name;
    server_info["version"] = info_.version;
    result["serverInfo"] = std::move(server_info);

    initialized_ = true;
    return result;
}

json::Value McpServer::build_tools_list() const {
    json::Value tools_array = json::Value::make_array();
    for (const auto& t : tools_) {
        json::Value tool = json::Value::make_object();
        tool["name"] = t.name;
        tool["description"] = t.description;
        tool["inputSchema"] = t.input_schema;
        tools_array.push_back(std::move(tool));
    }
    json::Value result = json::Value::make_object();
    result["tools"] = std::move(tools_array);
    return result;
}

json::Value McpServer::handle_tools_list() {
    return build_tools_list();
}

json::Value McpServer::handle_tools_call(const json::Value& params) {
    if (!params.is_object()) {
        throw std::runtime_error("Invalid params: expected object");
    }

    std::string tool_name;
    if (params.has("name") && params.at("name").is_string()) {
        tool_name = params.at("name").as_string();
    } else {
        throw std::runtime_error("Missing 'name' in tools/call params");
    }

    json::Value args = json::Value::make_object();
    if (params.has("arguments") && params.at("arguments").is_object()) {
        args = params.at("arguments");
    }

    auto it = tool_index_.find(tool_name);
    if (it == tool_index_.end()) {
        throw std::runtime_error("Unknown tool: " + tool_name);
    }

    // Execute the tool handler
    json::Value tool_result = tools_[it->second].handler(args);

    // MCP expects { content: [{type: "text", text: ...}], isError: false }
    // If the handler already returned this format, pass through.
    // Otherwise wrap the result.
    if (tool_result.is_object() && tool_result.has("content")) {
        return tool_result;
    }

    // Wrap raw result
    json::Value result = json::Value::make_object();
    json::Value content = json::Value::make_array();

    json::Value text_item = json::Value::make_object();
    text_item["type"] = "text";
    text_item["text"] = tool_result.dump(2);
    content.push_back(std::move(text_item));

    result["content"] = std::move(content);
    result["isError"] = false;
    return result;
}

json::Value McpServer::handle_ping() {
    return json::Value::make_object();
}

json::Value McpServer::handle_resources_list() {
    json::Value resources = json::Value::make_array();
    for (const auto& r : static_resources_) {
        json::Value res = json::Value::make_object();
        res["uri"] = r.uri;
        res["name"] = r.name;
        res["description"] = r.description;
        res["mimeType"] = r.mime_type;
        resources.push_back(std::move(res));
    }
    json::Value result = json::Value::make_object();
    result["resources"] = std::move(resources);
    return result;
}

json::Value McpServer::handle_resources_templates_list() {
    json::Value templates = json::Value::make_array();
    for (const auto& t : resource_templates_) {
        json::Value tmpl = json::Value::make_object();
        tmpl["uriTemplate"] = t.uri_template;
        tmpl["name"] = t.name;
        tmpl["description"] = t.description;
        tmpl["mimeType"] = t.mime_type;
        templates.push_back(std::move(tmpl));
    }
    json::Value result = json::Value::make_object();
    result["resourceTemplates"] = std::move(templates);
    return result;
}

// Match a URI against a template and extract variable values.
// Returns empty map if no match.
static std::map<std::string, std::string> match_uri_template(
    const std::string& uri, const std::string& tmpl) {

    std::map<std::string, std::string> vars;

    // Simple template matching: split on {var} placeholders
    std::string pattern = tmpl;
    size_t pos = 0;
    size_t uri_pos = 0;

    while (pos < pattern.size()) {
        // Find next {var}
        size_t brace_start = pattern.find('{', pos);
        if (brace_start == std::string::npos) {
            // Rest of pattern must match literally
            std::string literal = pattern.substr(pos);
            if (uri_pos + literal.size() > uri.size()) return {};
            if (uri.substr(uri_pos, literal.size()) != literal) return {};
            return vars; // full match
        }

        // Match literal part before {var}
        std::string literal = pattern.substr(pos, brace_start - pos);
        if (uri_pos + literal.size() > uri.size()) return {};
        if (uri.substr(uri_pos, literal.size()) != literal) return {};
        uri_pos += literal.size();

        // Find closing brace
        size_t brace_end = pattern.find('}', brace_start);
        if (brace_end == std::string::npos) return {};
        std::string var_name = pattern.substr(brace_start + 1, brace_end - brace_start - 1);
        pos = brace_end + 1;

        // Find the next literal in the pattern (or end) to delimit the variable
        size_t next_brace = pattern.find('{', pos);
        std::string next_literal;
        if (next_brace == std::string::npos) {
            next_literal = pattern.substr(pos);
        } else {
            next_literal = pattern.substr(pos, next_brace - pos);
        }

        // Extract variable value from URI
        if (next_literal.empty()) {
            // Variable extends to end of URI
            vars[var_name] = uri.substr(uri_pos);
            uri_pos = uri.size();
        } else {
            // URL-decode the delimiter search
            size_t delim = uri.find(next_literal, uri_pos);
            if (delim == std::string::npos) return {};
            vars[var_name] = uri.substr(uri_pos, delim - uri_pos);
            uri_pos = delim + next_literal.size();
        }
    }

    // Check URI is fully consumed
    if (uri_pos != uri.size()) return {};
    return vars;
}

// URL-decode a percent-encoded string
static std::string url_decode(const std::string& s) {
    std::string result;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = (s[i + 1] >= 'A') ? (s[i + 1] & 0xDF) - 'A' + 10 : s[i + 1] - '0';
            int lo = (s[i + 2] >= 'A') ? (s[i + 2] & 0xDF) - 'A' + 10 : s[i + 2] - '0';
            result.push_back(static_cast<char>(hi * 16 + lo));
            i += 2;
        } else if (s[i] == '+') {
            result.push_back(' ');
        } else {
            result.push_back(s[i]);
        }
    }
    return result;
}

json::Value McpServer::handle_resources_read(const json::Value& params) {
    std::string uri;
    if (params.has("uri") && params.at("uri").is_string()) {
        uri = params.at("uri").as_string();
    } else {
        throw std::runtime_error("Missing 'uri' in resources/read params");
    }

    json::Value content_value;

    // Try static resources first
    for (const auto& r : static_resources_) {
        if (r.uri == uri) {
            content_value = r.handler(uri);
            break;
        }
    }

    // Try resource templates
    if (content_value.is_null()) {
        for (const auto& tmpl : resource_templates_) {
            auto vars = match_uri_template(uri, tmpl.uri_template);
            if (!vars.empty()) {
                content_value = tmpl.handler(uri);
                break;
            }
        }
    }

    if (content_value.is_null()) {
        throw std::runtime_error("Unknown resource URI: " + uri);
    }

    // Wrap in MCP resource read response
    json::Value result = json::Value::make_object();
    json::Value contents = json::Value::make_array();
    json::Value item = json::Value::make_object();
    item["uri"] = uri;
    item["mimeType"] = "application/json";
    item["text"] = content_value.dump(2);
    contents.push_back(std::move(item));
    result["contents"] = std::move(contents);
    return result;
}

std::optional<json::Value> McpServer::handle_message(const json::Value& message) {
    if (!message.is_object()) return std::nullopt;

    json::Value id = message.has("id") ? message.at("id") : json::Value(nullptr);

    // Extract method
    if (!message.has("method") || !message.at("method").is_string()) {
        if (!id.is_null()) {
            return make_error(id, -32600, "Invalid request: missing method");
        }
        return std::nullopt;
    }

    std::string method = message.at("method").as_string();
    json::Value params = message.has("params") ? message.at("params")
                                                : json::Value::make_object();

    // Notification (no id) — process but don't respond
    bool is_notification = id.is_null();

    try {
        if (method == "initialize") {
            if (is_notification) return std::nullopt;
            return make_response(id, handle_initialize(params));
        }

        if (method == "notifications/initialized") {
            return std::nullopt; // notification, no response
        }

        // Before initialization, reject all other requests per MCP spec
        if (!initialized_) {
            if (!is_notification) {
                return make_error(id, -32002, "Server not initialized");
            }
            return std::nullopt;
        }

        if (method == "ping") {
            if (is_notification) return std::nullopt;
            return make_response(id, handle_ping());
        }

        if (method == "tools/list") {
            if (is_notification) return std::nullopt;
            return make_response(id, handle_tools_list());
        }

        if (method == "tools/call") {
            if (is_notification) return std::nullopt;
            return make_response(id, handle_tools_call(params));
        }

        if (method == "resources/list") {
            if (is_notification) return std::nullopt;
            return make_response(id, handle_resources_list());
        }

        if (method == "resources/templates/list") {
            if (is_notification) return std::nullopt;
            return make_response(id, handle_resources_templates_list());
        }

        if (method == "resources/read") {
            if (is_notification) return std::nullopt;
            return make_response(id, handle_resources_read(params));
        }

        // Unknown method
        if (!is_notification) {
            return make_error(id, -32601, "Method not found: " + method);
        }
        return std::nullopt;

    } catch (const std::exception& e) {
        if (!is_notification) {
            json::Value err_data = json::Value::make_object();
            err_data["detail"] = e.what();
            return make_error(id, -32603, "Internal error", err_data);
        }
        return std::nullopt;
    }
}

static std::mutex g_output_mutex;

void McpServer::run() {
    std::string line;

    // Read line-delimited JSON-RPC from stdin
    while (std::getline(std::cin, line)) {
        // Skip empty lines
        if (line.empty()) continue;

        // Remove trailing whitespace/CR
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        if (line.empty()) continue;

        std::string response = process_line(line);
        if (!response.empty()) {
            std::lock_guard<std::mutex> lock(g_output_mutex);
            std::cout << response << '\n';
            std::cout.flush();
        }
    }
}

std::string McpServer::process_line(const std::string& line) {
    json::Value msg;
    std::string err;
    if (!json::Value::try_parse(line, msg, err)) {
        // Parse error — respond with null id
        json::Value parse_error = make_error(nullptr, -32700,
            "Parse error: " + err);
        return parse_error.dump();
    }

    // Handle batch requests
    if (msg.is_array()) {
        json::Value responses = json::Value::make_array();
        for (size_t i = 0; i < msg.size(); ++i) {
            auto resp = handle_message(msg[i]);
            if (resp.has_value()) {
                responses.push_back(std::move(*resp));
            }
        }
        if (responses.size() == 0) return {};
        return responses.dump();
    }

    auto resp = handle_message(msg);
    if (!resp.has_value()) return {};
    return resp->dump();
}

} // namespace mcp


// ============================================================
// Tool definitions — register Everything SDK tools with the server
// ============================================================

namespace {

json::Value make_sort_enum() {
    json::Value arr = json::Value::make_array();
    // Only the most useful sort modes
    const char* sorts[] = {
        "name_ascending", "name_descending",
        "path_ascending", "path_descending",
        "size_ascending", "size_descending",
        "date_created_ascending", "date_created_descending",
        "date_modified_ascending", "date_modified_descending",
        "extension_ascending", "extension_descending",
        "attributes_ascending", "attributes_descending"
    };
    for (const char* s : sorts) arr.push_back(std::string(s));
    return arr;
}

int parse_sort(const std::string& sort_str) {
    if (sort_str == "name_ascending")              return 1;
    if (sort_str == "name_descending")             return 2;
    if (sort_str == "path_ascending")              return 3;
    if (sort_str == "path_descending")             return 4;
    if (sort_str == "size_ascending")              return 5;
    if (sort_str == "size_descending")             return 6;
    if (sort_str == "extension_ascending")         return 7;
    if (sort_str == "extension_descending")        return 8;
    if (sort_str == "date_created_ascending")      return 11;
    if (sort_str == "date_created_descending")     return 12;
    if (sort_str == "date_modified_ascending")     return 13;
    if (sort_str == "date_modified_descending")    return 14;
    if (sort_str == "attributes_ascending")        return 15;
    if (sort_str == "attributes_descending")       return 16;
    return 1; // default: name ascending
}

json::Value handle_search(const json::Value& args) {
    everything::SearchOptions opts;

    if (args.has("query") && args.at("query").is_string())
        opts.query = args.at("query").as_string();

    if (opts.query.empty())
        throw std::runtime_error("Parameter 'query' is required");

    if (args.has("match_path"))
        opts.match_path = args.at("match_path").as_bool();
    if (args.has("match_case"))
        opts.match_case = args.at("match_case").as_bool();
    if (args.has("match_whole_word"))
        opts.match_whole_word = args.at("match_whole_word").as_bool();
    if (args.has("regex"))
        opts.regex = args.at("regex").as_bool();
    if (args.has("max_results"))
        opts.max_results = static_cast<int>(args.at("max_results").as_int());
    if (args.has("offset"))
        opts.offset = static_cast<int>(args.at("offset").as_int());
    if (args.has("sort") && args.at("sort").is_string())
        opts.sort = parse_sort(args.at("sort").as_string());
    if (args.has("include_size"))
        opts.include_size = args.at("include_size").as_bool();
    if (args.has("include_dates"))
        opts.include_dates = args.at("include_dates").as_bool();

    auto resp = everything::search(opts);

    if (!resp.success) {
        json::Value result = json::Value::make_object();
        json::Value content = json::Value::make_array();
        json::Value text_item = json::Value::make_object();
        text_item["type"] = "text";
        text_item["text"] = "Error: " + resp.error_message;
        content.push_back(std::move(text_item));
        result["content"] = std::move(content);
        result["isError"] = true;
        return result;
    }

    // Build structured JSON result
    json::Value result = json::Value::make_object();
    result["total_results"] = resp.total_results;
    result["returned_results"] = resp.returned_results;
    result["offset"] = resp.offset;

    json::Value results_arr = json::Value::make_array();
    for (const auto& r : resp.results) {
        json::Value item = json::Value::make_object();
        item["index"] = r.index;
        item["file_name"] = r.file_name;
        item["path"] = r.path;
        item["full_path"] = r.full_path;
        item["extension"] = r.extension;
        item["type"] = r.is_folder ? "folder" : (r.is_file ? "file" : "unknown");

        if (r.size.has_value()) {
            item["size"] = *r.size;
        }

        if (!r.dates.created.empty())  item["date_created"] = r.dates.created;
        if (!r.dates.modified.empty()) item["date_modified"] = r.dates.modified;
        if (!r.dates.accessed.empty()) item["date_accessed"] = r.dates.accessed;

        // Attribute flags
        json::Value attrs = json::Value::make_object();
        attrs["value"] = static_cast<int64_t>(r.attributes);
        std::string attr_str;
        if (r.attributes & FILE_ATTRIBUTE_READONLY)   attr_str += 'R';
        if (r.attributes & FILE_ATTRIBUTE_HIDDEN)     attr_str += 'H';
        if (r.attributes & FILE_ATTRIBUTE_SYSTEM)     attr_str += 'S';
        if (r.attributes & FILE_ATTRIBUTE_DIRECTORY)  attr_str += 'D';
        if (r.attributes & FILE_ATTRIBUTE_ARCHIVE)    attr_str += 'A';
        attrs["flags"] = attr_str.empty() ? "N" : attr_str;
        item["attributes"] = std::move(attrs);

        results_arr.push_back(std::move(item));
    }

    result["results"] = std::move(results_arr);

    // Wrap in MCP content format
    json::Value mcp_result = json::Value::make_object();
    json::Value content = json::Value::make_array();
    json::Value text_item = json::Value::make_object();
    text_item["type"] = "text";
    text_item["text"] = result.dump(2);
    content.push_back(std::move(text_item));
    mcp_result["content"] = std::move(content);
    mcp_result["isError"] = false;
    return mcp_result;
}

json::Value handle_get_count(const json::Value& args) {
    std::string query;
    if (args.has("query") && args.at("query").is_string())
        query = args.at("query").as_string();
    if (query.empty())
        throw std::runtime_error("Parameter 'query' is required");

    int count = everything::get_result_count(query);

    json::Value result = json::Value::make_object();
    result["query"] = query;
    result["total_results"] = count;

    json::Value mcp_result = json::Value::make_object();
    json::Value content = json::Value::make_array();
    json::Value text_item = json::Value::make_object();
    text_item["type"] = "text";
    text_item["text"] = result.dump(2);
    content.push_back(std::move(text_item));
    mcp_result["content"] = std::move(content);
    return mcp_result;
}

json::Value handle_is_running(const json::Value&) {
    bool running = everything::is_running();
    json::Value result = json::Value::make_object();
    result["running"] = running;
    result["sdk_loaded"] = true; // If we got here, SDK is loaded

    json::Value mcp_result = json::Value::make_object();
    json::Value content = json::Value::make_array();
    json::Value text_item = json::Value::make_object();
    text_item["type"] = "text";
    text_item["text"] = result.dump(2);
    content.push_back(std::move(text_item));
    mcp_result["content"] = std::move(content);
    return mcp_result;
}

json::Value handle_get_version(const json::Value&) {
    auto vi = everything::get_version();
    json::Value result = json::Value::make_object();
    result["available"] = vi.available;
    if (vi.available) {
        result["major"] = vi.major;
        result["minor"] = vi.minor;
        result["revision"] = vi.revision;
        result["build"] = vi.build;
        result["version_string"] = std::to_string(vi.major) + "." +
            std::to_string(vi.minor) + "." +
            std::to_string(vi.revision) + "." +
            std::to_string(vi.build);
    }

    json::Value mcp_result = json::Value::make_object();
    json::Value content = json::Value::make_array();
    json::Value text_item = json::Value::make_object();
    text_item["type"] = "text";
    text_item["text"] = result.dump(2);
    content.push_back(std::move(text_item));
    mcp_result["content"] = std::move(content);
    return mcp_result;
}

// ============================================================
// Smart query helpers
// ============================================================

// Parse a duration string like "5m", "1h", "3d", "2w" into Everything's
// relative-time syntax: last5mins, last1hours, last3days, last2weeks.
// Falls back to raw date strings (YYYY-MM-DD) with a ">" prefix.
std::string parse_duration(const std::string& since) {
    if (since.empty()) return "last1hours";

    // If it looks like a date (contains '-')
    if (since.find('-') != std::string::npos) {
        return ">\"" + since + "\"";
    }

    if (since.size() < 2) return "last1hours";

    char unit = static_cast<char>(::tolower(since.back()));
    std::string num = since.substr(0, since.size() - 1);

    // Validate num is numeric
    for (char c : num) {
        if (c < '0' || c > '9') return "last1hours";
    }

    switch (unit) {
        case 'm': return "last" + num + "mins";
        case 'h': return "last" + num + "hours";
        case 'd': return "last" + num + "days";
        case 'w': return "last" + num + "weeks";
        default:  return "last1hours";
    }
}

// Helper: format a search error into MCP content
json::Value make_error_result(const std::string& message) {
    json::Value result = json::Value::make_object();
    json::Value content = json::Value::make_array();
    json::Value text_item = json::Value::make_object();
    text_item["type"] = "text";
    text_item["text"] = "Error: " + message;
    content.push_back(std::move(text_item));
    result["content"] = std::move(content);
    result["isError"] = true;
    return result;
}

// Helper: format a JSON value into MCP content response
json::Value make_content_result(json::Value data) {
    json::Value result = json::Value::make_object();
    json::Value content = json::Value::make_array();
    json::Value text_item = json::Value::make_object();
    text_item["type"] = "text";
    text_item["text"] = data.dump(2);
    content.push_back(std::move(text_item));
    result["content"] = std::move(content);
    result["isError"] = false;
    return result;
}

// ============================================================
// search_content handler
// ============================================================

json::Value handle_search_content(const json::Value& args) {
    content_search::SearchOptions opts;

    if (args.has("pattern") && args.at("pattern").is_string())
        opts.pattern = args.at("pattern").as_string();
    if (opts.pattern.empty())
        throw std::runtime_error("Parameter 'pattern' is required");

    if (args.has("path") && args.at("path").is_string())
        opts.path_filter = args.at("path").as_string();

    if (args.has("extensions") && args.at("extensions").is_array()) {
        for (size_t i = 0; i < args.at("extensions").size(); ++i) {
            const auto& ext_val = args.at("extensions")[i];
            if (ext_val.is_string())
                opts.extensions.push_back(ext_val.as_string());
        }
    }

    if (args.has("match_case"))  opts.match_case = args.at("match_case").as_bool();
    if (args.has("regex"))       opts.regex = args.at("regex").as_bool();
    if (args.has("max_files"))   opts.max_files = static_cast<int>(args.at("max_files").as_int());
    if (args.has("context_lines")) opts.context_lines = static_cast<int>(args.at("context_lines").as_int());
    if (args.has("max_results")) opts.max_results = static_cast<int>(args.at("max_results").as_int());

    auto resp = content_search::search(opts);

    if (!resp.success) {
        return make_error_result(resp.error_message);
    }

    json::Value result = json::Value::make_object();
    result["files_searched"] = resp.files_searched;
    result["total_matches"] = resp.total_matches;

    json::Value matches_arr = json::Value::make_array();
    for (const auto& m : resp.matches) {
        json::Value match = json::Value::make_object();
        match["file_path"] = m.file_path;
        match["line_number"] = m.line_number;
        match["match_line"] = m.match_line;

        if (!m.context_before.empty()) {
            json::Value before = json::Value::make_array();
            for (const auto& line : m.context_before) before.push_back(line);
            match["context_before"] = std::move(before);
        }
        if (!m.context_after.empty()) {
            json::Value after = json::Value::make_array();
            for (const auto& line : m.context_after) after.push_back(line);
            match["context_after"] = std::move(after);
        }

        matches_arr.push_back(std::move(match));
    }

    result["matches"] = std::move(matches_arr);
    return make_content_result(std::move(result));
}

// ============================================================
// recently_changed handler
// ============================================================

json::Value handle_recently_changed(const json::Value& args) {
    std::string since = "1h";
    if (args.has("since") && args.at("since").is_string())
        since = args.at("since").as_string();

    std::string date_field = "dm"; // date modified
    if (args.has("date_type") && args.at("date_type").is_string()) {
        std::string dt = args.at("date_type").as_string();
        if (dt == "created")       date_field = "dc";
        else if (dt == "accessed") date_field = "da";
    }

    std::string query = date_field + ":" + parse_duration(since);

    if (args.has("path") && args.at("path").is_string())
        query += " path:\"" + args.at("path").as_string() + "\"";
    if (args.has("ext") && args.at("ext").is_string())
        query += " ext:" + args.at("ext").as_string();
    if (args.has("glob") && args.at("glob").is_string())
        query += " " + args.at("glob").as_string();

    everything::SearchOptions es_opts;
    es_opts.query = query;
    es_opts.sort = (date_field == "dm") ? 14 : (date_field == "dc" ? 12 : 24);
    es_opts.include_size = args.has("include_size") ? args.at("include_size").as_bool() : true;
    es_opts.include_dates = true;
    es_opts.max_results = args.has("max_results")
        ? static_cast<int>(args.at("max_results").as_int()) : 50;

    auto es_resp = everything::search(es_opts);
    if (!es_resp.success)
        return make_error_result(es_resp.error_message);

    json::Value result = json::Value::make_object();
    result["query"] = query;
    result["since"] = since;
    result["total_results"] = es_resp.total_results;
    result["returned_results"] = es_resp.returned_results;

    json::Value results_arr = json::Value::make_array();
    for (const auto& r : es_resp.results) {
        json::Value item = json::Value::make_object();
        item["file_name"] = r.file_name;
        item["full_path"] = r.full_path;
        item["type"] = r.is_folder ? "folder" : "file";
        if (r.size.has_value()) item["size"] = *r.size;
        if (!r.dates.modified.empty()) item["date_modified"] = r.dates.modified;
        if (!r.dates.created.empty()) item["date_created"] = r.dates.created;
        results_arr.push_back(std::move(item));
    }
    result["results"] = std::move(results_arr);
    return make_content_result(std::move(result));
}

// ============================================================
// largest_files handler
// ============================================================

json::Value handle_largest_files(const json::Value& args) {
    std::string query;

    if (args.has("path") && args.at("path").is_string())
        query += " path:\"" + args.at("path").as_string() + "\"";
    if (args.has("ext") && args.at("ext").is_string())
        query += " ext:" + args.at("ext").as_string();
    if (args.has("glob") && args.at("glob").is_string())
        query += " " + args.at("glob").as_string();

    // Exclude folders
    query += " !folder";

    if (query.empty()) query = "sort:size_descending";
    if (query[0] == ' ') query.erase(0, 1);

    everything::SearchOptions es_opts;
    es_opts.query = query;
    es_opts.sort = 6; // size_descending
    es_opts.include_size = true;
    es_opts.include_dates = false;
    es_opts.max_results = args.has("top")
        ? static_cast<int>(args.at("top").as_int()) : 20;

    auto es_resp = everything::search(es_opts);
    if (!es_resp.success)
        return make_error_result(es_resp.error_message);

    json::Value result = json::Value::make_object();
    result["total_results"] = es_resp.total_results;
    result["returned_results"] = es_resp.returned_results;

    json::Value results_arr = json::Value::make_array();
    for (const auto& r : es_resp.results) {
        json::Value item = json::Value::make_object();
        item["file_name"] = r.file_name;
        item["full_path"] = r.full_path;
        item["size"] = r.size.value_or(0);
        results_arr.push_back(std::move(item));
    }
    result["results"] = std::move(results_arr);
    return make_content_result(std::move(result));
}

// ============================================================
// duplicates handler — supports name-based and hash-based dedup
// ============================================================

// Forward declaration — defined below
json::Value handle_duplicates_hash(const json::Value& args);

json::Value handle_duplicates(const json::Value& args) {
    std::string method = "name";
    if (args.has("method") && args.at("method").is_string())
        method = args.at("method").as_string();

    if (method == "hash") {
        return handle_duplicates_hash(args);
    }

    // Default: name-based dedup (original behavior)
    std::string query = "dupe:";

    if (args.has("path") && args.at("path").is_string())
        query += " path:\"" + args.at("path").as_string() + "\"";

    if (args.has("min_size"))
        query += " size:>=" + std::to_string(args.at("min_size").as_int());

    everything::SearchOptions es_opts;
    es_opts.query = query;
    es_opts.include_size = true;
    es_opts.include_dates = false;
    es_opts.max_results = args.has("max_results")
        ? static_cast<int>(args.at("max_results").as_int()) : 200;

    auto es_resp = everything::search(es_opts);
    if (!es_resp.success)
        return make_error_result(es_resp.error_message);

    // Group results by file name
    std::map<std::string, std::vector<const everything::SearchResult*>> groups;
    for (const auto& r : es_resp.results) {
        groups[r.file_name].push_back(&r);
    }

    json::Value result = json::Value::make_object();
    result["method"] = "name";
    result["total_files"] = static_cast<int64_t>(es_resp.returned_results);
    result["duplicate_groups"] = 0;

    json::Value groups_arr = json::Value::make_array();
    int group_count = 0;
    for (const auto& [name, files] : groups) {
        if (files.size() < 2) continue;
        ++group_count;

        json::Value group = json::Value::make_object();
        group["file_name"] = name;
        group["count"] = static_cast<int64_t>(files.size());

        json::Value copies = json::Value::make_array();
        for (const auto* f : files) {
            json::Value copy = json::Value::make_object();
            copy["full_path"] = f->full_path;
            copy["size"] = f->size.value_or(0);
            copies.push_back(std::move(copy));
        }
        group["copies"] = std::move(copies);
        groups_arr.push_back(std::move(group));
    }

    result["duplicate_groups"] = group_count;
    result["groups"] = std::move(groups_arr);
    return make_content_result(std::move(result));
}

// Hash-based dedup: dupe:size → group by size → MD5 within groups → merge by hash
json::Value handle_duplicates_hash(const json::Value& args) {
    // Step 1: Everything query dupe:size to pre-filter to files sharing sizes
    std::string query = "dupe:size";

    if (args.has("path") && args.at("path").is_string())
        query += " path:\"" + args.at("path").as_string() + "\"";
    if (args.has("min_size"))
        query += " size:>=" + std::to_string(args.at("min_size").as_int());

    int max_files = args.has("max_results")
        ? static_cast<int>(args.at("max_results").as_int()) : 500;

    everything::SearchOptions es_opts;
    es_opts.query = query;
    es_opts.include_size = true;
    es_opts.include_dates = false;
    es_opts.max_results = max_files;

    auto es_resp = everything::search(es_opts);
    if (!es_resp.success)
        return make_error_result(es_resp.error_message);

    // Step 2: Group results by file size
    std::map<int64_t, std::vector<const everything::SearchResult*>> size_groups;
    for (const auto& r : es_resp.results) {
        if (r.size.has_value()) {
            size_groups[*r.size].push_back(&r);
        }
    }

    // Step 3: For each size group with ≥2 files, compute MD5 hashes
    file_hashing::Md5Context ctx;
    std::map<std::string, std::vector<const everything::SearchResult*>> hash_groups;
    int files_hashed = 0;

    for (const auto& [size, files] : size_groups) {
        if (files.size() < 2) continue;

        for (const auto* f : files) {
            auto hash = ctx.hash_file(f->full_path);
            if (hash) {
                hash_groups[*hash].push_back(f);
                ++files_hashed;
            }
        }
    }

    // Step 4: Build result — only groups with ≥2 files sharing the same hash
    json::Value result = json::Value::make_object();
    result["method"] = "hash";
    result["files_found"] = static_cast<int64_t>(es_resp.returned_results);
    result["files_hashed"] = files_hashed;

    json::Value groups_arr = json::Value::make_array();
    int group_count = 0;
    int64_t wasted_space = 0;

    for (const auto& [hash, files] : hash_groups) {
        if (files.size() < 2) continue;
        ++group_count;

        json::Value group = json::Value::make_object();
        group["md5"] = hash;
        group["count"] = static_cast<int64_t>(files.size());
        group["size"] = files[0]->size.value_or(0);
        wasted_space += (static_cast<int64_t>(files.size()) - 1) *
                        files[0]->size.value_or(0);

        json::Value copies = json::Value::make_array();
        for (const auto* f : files) {
            json::Value copy = json::Value::make_object();
            copy["full_path"] = f->full_path;
            copy["file_name"] = f->file_name;
            copy["size"] = f->size.value_or(0);
            copies.push_back(std::move(copy));
        }
        group["copies"] = std::move(copies);
        groups_arr.push_back(std::move(group));
    }

    result["duplicate_groups"] = group_count;
    result["wasted_space"] = wasted_space;
    result["groups"] = std::move(groups_arr);
    return make_content_result(std::move(result));
}

// ============================================================
// smart_search handler — NL to Everything query builder
// ============================================================

json::Value handle_smart_search(const json::Value& args) {
    std::string intent;
    if (args.has("intent") && args.at("intent").is_string())
        intent = args.at("intent").as_string();
    if (intent.empty())
        throw std::runtime_error("Parameter 'intent' is required");

    bool execute = false;
    if (args.has("execute"))
        execute = args.at("execute").as_bool();

    auto built = smart_query::build(intent);

    json::Value result = json::Value::make_object();
    result["intent"] = intent;
    result["query"] = built.query;

    json::Value tokens = json::Value::make_array();
    for (const auto& t : built.tokens) tokens.push_back(t);
    result["tokens"] = std::move(tokens);

    result["has_file_type"]   = built.has_file_type;
    result["has_size_filter"] = built.has_size_filter;
    result["has_date_filter"] = built.has_date_filter;
    result["has_path_filter"] = built.has_path_filter;

    if (execute && !built.query.empty()) {
        // Run the generated query
        everything::SearchOptions es_opts;
        es_opts.query = built.query;
        es_opts.include_size = true;
        es_opts.include_dates = true;
        es_opts.max_results = args.has("max_results")
            ? static_cast<int>(args.at("max_results").as_int()) : 50;

        auto es_resp = everything::search(es_opts);
        if (!es_resp.success) {
            result["error"] = es_resp.error_message;
        } else {
            result["total_results"] = es_resp.total_results;
            result["returned_results"] = es_resp.returned_results;

            json::Value results_arr = json::Value::make_array();
            for (const auto& r : es_resp.results) {
                json::Value item = json::Value::make_object();
                item["file_name"] = r.file_name;
                item["full_path"] = r.full_path;
                item["type"] = r.is_folder ? "folder" : "file";
                if (r.size.has_value()) item["size"] = *r.size;
                results_arr.push_back(std::move(item));
            }
            result["results"] = std::move(results_arr);
        }
    }

    return make_content_result(std::move(result));
}

// ============================================================
// open handler — open file with default application
// ============================================================

json::Value handle_open(const json::Value& args) {
    std::string path;
    if (args.has("path") && args.at("path").is_string())
        path = args.at("path").as_string();
    if (path.empty())
        throw std::runtime_error("Parameter 'path' is required");

    auto res = actions::open(path);

    json::Value result = json::Value::make_object();
    result["success"] = res.success;
    result["action"] = res.action;
    result["path"] = res.path;
    if (!res.error_message.empty())
        result["error"] = res.error_message;
    return make_content_result(std::move(result));
}

// ============================================================
// reveal handler — reveal file in Explorer
// ============================================================

json::Value handle_reveal(const json::Value& args) {
    std::string path;
    if (args.has("path") && args.at("path").is_string())
        path = args.at("path").as_string();
    if (path.empty())
        throw std::runtime_error("Parameter 'path' is required");

    auto res = actions::reveal(path);

    json::Value result = json::Value::make_object();
    result["success"] = res.success;
    result["action"] = res.action;
    result["path"] = res.path;
    if (!res.error_message.empty())
        result["error"] = res.error_message;
    return make_content_result(std::move(result));
}

// ============================================================
// copy_path handler — copy path to clipboard
// ============================================================

json::Value handle_copy_path(const json::Value& args) {
    std::string path;
    if (args.has("path") && args.at("path").is_string())
        path = args.at("path").as_string();
    if (path.empty())
        throw std::runtime_error("Parameter 'path' is required");

    auto res = actions::copy_path(path);

    json::Value result = json::Value::make_object();
    result["success"] = res.success;
    result["action"] = res.action;
    result["path"] = res.path;
    if (!res.error_message.empty())
        result["error"] = res.error_message;
    return make_content_result(std::move(result));
}

// ============================================================
// preview handler — unified file preview with metadata extraction
// ============================================================

json::Value handle_preview(const json::Value& args) {
    std::string path;
    if (args.has("path") && args.at("path").is_string())
        path = args.at("path").as_string();
    if (path.empty())
        throw std::runtime_error("Parameter 'path' is required");

    int max_lines = 100;
    if (args.has("max_lines"))
        max_lines = static_cast<int>(args.at("max_lines").as_int());

    auto pr = file_preview::preview(path, max_lines);

    if (!pr.error_message.empty()) {
        return make_error_result(pr.error_message);
    }

    json::Value result = json::Value::make_object();
    result["category"] = pr.category;

    // Basic info
    json::Value basic = json::Value::make_object();
    basic["path"] = pr.basic.path;
    basic["file_name"] = pr.basic.file_name;
    basic["extension"] = pr.basic.extension;
    basic["size"] = pr.basic.size;
    basic["is_folder"] = pr.basic.is_folder;
    if (!pr.basic.date_modified.empty())
        basic["date_modified"] = pr.basic.date_modified;
    if (!pr.basic.date_created.empty())
        basic["date_created"] = pr.basic.date_created;
    result["file_info"] = std::move(basic);

    // Text preview
    if (pr.text) {
        json::Value text = json::Value::make_object();
        text["total_lines"] = pr.text->total_lines;
        text["shown_lines"] = pr.text->shown_lines;
        text["truncated"] = pr.text->truncated;

        json::Value lines = json::Value::make_array();
        for (const auto& line : pr.text->lines) lines.push_back(line);
        text["lines"] = std::move(lines);
        result["text_preview"] = std::move(text);
    }

    // PE metadata
    if (pr.pe) {
        json::Value pe = json::Value::make_object();
        if (!pr.pe->file_version.empty())      pe["file_version"] = pr.pe->file_version;
        if (!pr.pe->product_version.empty())   pe["product_version"] = pr.pe->product_version;
        if (!pr.pe->company_name.empty())      pe["company_name"] = pr.pe->company_name;
        if (!pr.pe->file_description.empty())  pe["file_description"] = pr.pe->file_description;
        if (!pr.pe->product_name.empty())      pe["product_name"] = pr.pe->product_name;
        if (!pr.pe->original_filename.empty()) pe["original_filename"] = pr.pe->original_filename;
        if (!pr.pe->internal_name.empty())     pe["internal_name"] = pr.pe->internal_name;
        if (!pr.pe->legal_copyright.empty())   pe["legal_copyright"] = pr.pe->legal_copyright;
        result["pe_metadata"] = std::move(pe);
    }

    // Image metadata
    if (pr.image) {
        json::Value img = json::Value::make_object();
        img["format"] = pr.image->format;
        img["width"] = pr.image->width;
        img["height"] = pr.image->height;
        if (!pr.image->details.empty())
            img["details"] = pr.image->details;
        result["image"] = std::move(img);
    }

    // Archive listing
    if (pr.archive) {
        json::Value arch = json::Value::make_object();
        arch["entry_count"] = pr.archive->entry_count;
        arch["total_uncompressed"] = pr.archive->total_uncompressed;
        arch["truncated"] = pr.archive->truncated;

        json::Value entries = json::Value::make_array();
        for (const auto& e : pr.archive->entries) {
            json::Value entry = json::Value::make_object();
            entry["name"] = e.name;
            entry["uncompressed_size"] = e.uncompressed_size;
            entry["compressed_size"] = e.compressed_size;
            entries.push_back(std::move(entry));
        }
        arch["entries"] = std::move(entries);
        result["archive"] = std::move(arch);
    }

    return make_content_result(std::move(result));
}

} // anonymous namespace

namespace mcp {

void register_everything_tools(McpServer& server) {
    // --- search tool ---
    {
        mcp::ToolDef def;
        def.name = "search";
        def.description =
            "Search for files and folders using Everything's instant search. "
            "Supports wildcards (*, ?), boolean operators (|, &, !), "
            "and full-text search. Example: 'ext:cpp sort:size_descending' "
            "finds C++ files sorted by size.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";

        json::Value props = json::Value::make_object();

        json::Value p_query = json::Value::make_object();
        p_query["type"] = "string";
        p_query["description"] = "Search query string (Everything syntax)";
        props["query"] = std::move(p_query);

        json::Value p_mp = json::Value::make_object();
        p_mp["type"] = "boolean";
        p_mp["default"] = false;
        p_mp["description"] = "Match against full path, not just filename";
        props["match_path"] = std::move(p_mp);

        json::Value p_mc = json::Value::make_object();
        p_mc["type"] = "boolean";
        p_mc["default"] = false;
        p_mc["description"] = "Case-sensitive search";
        props["match_case"] = std::move(p_mc);

        json::Value p_mw = json::Value::make_object();
        p_mw["type"] = "boolean";
        p_mw["default"] = false;
        p_mw["description"] = "Match whole words only";
        props["match_whole_word"] = std::move(p_mw);

        json::Value p_re = json::Value::make_object();
        p_re["type"] = "boolean";
        p_re["default"] = false;
        p_re["description"] = "Treat query as a regular expression";
        props["regex"] = std::move(p_re);

        json::Value p_max = json::Value::make_object();
        p_max["type"] = "integer";
        p_max["default"] = 100;
        p_max["minimum"] = 1;
        p_max["maximum"] = 10000;
        p_max["description"] = "Maximum number of results to return";
        props["max_results"] = std::move(p_max);

        json::Value p_off = json::Value::make_object();
        p_off["type"] = "integer";
        p_off["default"] = 0;
        p_off["minimum"] = 0;
        p_off["description"] = "Number of results to skip (for pagination)";
        props["offset"] = std::move(p_off);

        json::Value p_sort = json::Value::make_object();
        p_sort["type"] = "string";
        p_sort["enum"] = make_sort_enum();
        p_sort["default"] = "name_ascending";
        p_sort["description"] = "Sort order for results";
        props["sort"] = std::move(p_sort);

        json::Value p_size = json::Value::make_object();
        p_size["type"] = "boolean";
        p_size["default"] = true;
        p_size["description"] = "Include file size in results";
        props["include_size"] = std::move(p_size);

        json::Value p_dates = json::Value::make_object();
        p_dates["type"] = "boolean";
        p_dates["default"] = true;
        p_dates["description"] = "Include creation/modification/access dates";
        props["include_dates"] = std::move(p_dates);

        schema["properties"] = std::move(props);

        json::Value required = json::Value::make_array();
        required.push_back("query");
        schema["required"] = std::move(required);

        def.input_schema = std::move(schema);
        def.handler = handle_search;
        server.register_tool(std::move(def));
    }

    // --- get_count tool ---
    {
        mcp::ToolDef def;
        def.name = "get_count";
        def.description =
            "Get the total number of results matching a query without "
            "fetching the actual file paths. Useful for quick checks.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";

        json::Value props = json::Value::make_object();
        json::Value p = json::Value::make_object();
        p["type"] = "string";
        p["description"] = "Search query string";
        props["query"] = std::move(p);
        schema["properties"] = std::move(props);

        json::Value req = json::Value::make_array();
        req.push_back("query");
        schema["required"] = std::move(req);

        def.input_schema = std::move(schema);
        def.handler = handle_get_count;
        server.register_tool(std::move(def));
    }

    // --- is_running tool ---
    {
        mcp::ToolDef def;
        def.name = "is_running";
        def.description = "Check if the Everything search service is running and reachable.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";
        schema["properties"] = json::Value::make_object();

        def.input_schema = std::move(schema);
        def.handler = handle_is_running;
        server.register_tool(std::move(def));
    }

    // --- get_version tool ---
    {
        mcp::ToolDef def;
        def.name = "get_version";
        def.description = "Get the Everything search engine version information.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";
        schema["properties"] = json::Value::make_object();

        def.input_schema = std::move(schema);
        def.handler = handle_get_version;
        server.register_tool(std::move(def));
    }

    // --- search_content tool ---
    {
        mcp::ToolDef def;
        def.name = "search_content";
        def.description =
            "Search inside file contents for a text pattern or regex. "
            "Returns matching lines with surrounding context. "
            "Uses Everything to find candidate text files, then scans each one. "
            "Example: find 'TODO' in all .cpp files under a project directory.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";

        json::Value props = json::Value::make_object();

        json::Value p_pattern = json::Value::make_object();
        p_pattern["type"] = "string";
        p_pattern["description"] = "Text pattern or regex to search for inside files";
        props["pattern"] = std::move(p_pattern);

        json::Value p_path = json::Value::make_object();
        p_path["type"] = "string";
        p_path["description"] = "Optional path scope (Everything path: filter)";
        props["path"] = std::move(p_path);

        json::Value p_exts = json::Value::make_object();
        p_exts["type"] = "array";
        p_exts["items"]["type"] = "string";
        p_exts["description"] = "File extensions to search (default: common text/code files)";
        props["extensions"] = std::move(p_exts);

        json::Value p_mc = json::Value::make_object();
        p_mc["type"] = "boolean";
        p_mc["default"] = false;
        p_mc["description"] = "Case-sensitive matching";
        props["match_case"] = std::move(p_mc);

        json::Value p_re = json::Value::make_object();
        p_re["type"] = "boolean";
        p_re["default"] = false;
        p_re["description"] = "Treat pattern as a regular expression";
        props["regex"] = std::move(p_re);

        json::Value p_mf = json::Value::make_object();
        p_mf["type"] = "integer";
        p_mf["default"] = 200;
        p_mf["description"] = "Maximum files to scan";
        props["max_files"] = std::move(p_mf);

        json::Value p_cl = json::Value::make_object();
        p_cl["type"] = "integer";
        p_cl["default"] = 2;
        p_cl["description"] = "Context lines before and after each match";
        props["context_lines"] = std::move(p_cl);

        json::Value p_mr = json::Value::make_object();
        p_mr["type"] = "integer";
        p_mr["default"] = 50;
        p_mr["description"] = "Maximum total matches to return";
        props["max_results"] = std::move(p_mr);

        schema["properties"] = std::move(props);

        json::Value req = json::Value::make_array();
        req.push_back("pattern");
        schema["required"] = std::move(req);

        def.input_schema = std::move(schema);
        def.handler = handle_search_content;
        server.register_tool(std::move(def));
    }

    // --- recently_changed tool ---
    {
        mcp::ToolDef def;
        def.name = "recently_changed";
        def.description =
            "Find files modified within a recent time window. "
            "Uses Everything's date filters. "
            "Example: 'since:1h' finds files changed in the last hour; "
            "'since:2024-06-01' finds files since a specific date.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";

        json::Value props = json::Value::make_object();

        json::Value p_since = json::Value::make_object();
        p_since["type"] = "string";
        p_since["default"] = "1h";
        p_since["description"] = "Time window: '5m' (mins), '1h' (hours), '2d' (days), '1w' (weeks), or ISO date 'YYYY-MM-DD'";
        props["since"] = std::move(p_since);

        json::Value p_dt = json::Value::make_object();
        p_dt["type"] = "string";
        {
            json::Value enum_arr = json::Value::make_array();
            enum_arr.push_back("modified");
            enum_arr.push_back("created");
            enum_arr.push_back("accessed");
            p_dt["enum"] = std::move(enum_arr);
        }
        p_dt["default"] = "modified";
        p_dt["description"] = "Which date to check";
        props["date_type"] = std::move(p_dt);

        json::Value p_path = json::Value::make_object();
        p_path["type"] = "string";
        p_path["description"] = "Optional path scope";
        props["path"] = std::move(p_path);

        json::Value p_ext = json::Value::make_object();
        p_ext["type"] = "string";
        p_ext["description"] = "Optional extension filter (e.g., 'cpp')";
        props["ext"] = std::move(p_ext);

        json::Value p_max = json::Value::make_object();
        p_max["type"] = "integer";
        p_max["default"] = 50;
        p_max["description"] = "Maximum results";
        props["max_results"] = std::move(p_max);

        schema["properties"] = std::move(props);
        def.input_schema = std::move(schema);
        def.handler = handle_recently_changed;
        server.register_tool(std::move(def));
    }

    // --- largest_files tool ---
    {
        mcp::ToolDef def;
        def.name = "largest_files";
        def.description =
            "Find the largest files in a directory or across the system. "
            "Returns files sorted by size descending.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";

        json::Value props = json::Value::make_object();

        json::Value p_path = json::Value::make_object();
        p_path["type"] = "string";
        p_path["description"] = "Optional path scope";
        props["path"] = std::move(p_path);

        json::Value p_ext = json::Value::make_object();
        p_ext["type"] = "string";
        p_ext["description"] = "Optional extension filter";
        props["ext"] = std::move(p_ext);

        json::Value p_top = json::Value::make_object();
        p_top["type"] = "integer";
        p_top["default"] = 20;
        p_top["description"] = "Number of results (top N largest)";
        props["top"] = std::move(p_top);

        schema["properties"] = std::move(props);
        def.input_schema = std::move(schema);
        def.handler = handle_largest_files;
        server.register_tool(std::move(def));
    }

    // --- duplicates tool ---
    {
        mcp::ToolDef def;
        def.name = "duplicates";
        def.description =
            "Find duplicate files. Method 'name' groups by filename (Everything dupe:). "
            "Method 'hash' groups by MD5 content hash (dupe:size pre-filter, then CNG hashing) "
            "for accurate content-based dedup with wasted-space calculation.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";

        json::Value props = json::Value::make_object();

        json::Value p_method = json::Value::make_object();
        p_method["type"] = "string";
        {
            json::Value enum_arr = json::Value::make_array();
            enum_arr.push_back("name");
            enum_arr.push_back("hash");
            p_method["enum"] = std::move(enum_arr);
        }
        p_method["default"] = "name";
        p_method["description"] = "Dedup method: 'name' (fast, by filename) or 'hash' (accurate, by MD5 content)";
        props["method"] = std::move(p_method);

        json::Value p_path = json::Value::make_object();
        p_path["type"] = "string";
        p_path["description"] = "Optional path scope";
        props["path"] = std::move(p_path);

        json::Value p_minsize = json::Value::make_object();
        p_minsize["type"] = "integer";
        p_minsize["description"] = "Minimum file size in bytes";
        props["min_size"] = std::move(p_minsize);

        json::Value p_max = json::Value::make_object();
        p_max["type"] = "integer";
        p_max["default"] = 200;
        p_max["description"] = "Maximum files to check (500 for hash mode)";
        props["max_results"] = std::move(p_max);

        schema["properties"] = std::move(props);
        def.input_schema = std::move(schema);
        def.handler = handle_duplicates;
        server.register_tool(std::move(def));
    }

    // --- smart_search tool ---
    {
        mcp::ToolDef def;
        def.name = "smart_search";
        def.description =
            "Translate a natural-language intent into Everything search syntax. "
            "Recognizes file types (pdfs, images, code), size qualifiers (large, huge, 10mb), "
            "time filters (today, last week, recent), and path scopes. "
            "Example: 'find large pdfs from last month' -> 'ext:pdf dm:last30days size:>10mb'. "
            "Set execute=true to run the query immediately.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";

        json::Value props = json::Value::make_object();

        json::Value p_intent = json::Value::make_object();
        p_intent["type"] = "string";
        p_intent["description"] = "Natural language search intent (e.g., 'large pdfs from last month')";
        props["intent"] = std::move(p_intent);

        json::Value p_exec = json::Value::make_object();
        p_exec["type"] = "boolean";
        p_exec["default"] = false;
        p_exec["description"] = "If true, execute the generated query and return results";
        props["execute"] = std::move(p_exec);

        json::Value p_max = json::Value::make_object();
        p_max["type"] = "integer";
        p_max["default"] = 50;
        p_max["description"] = "Maximum results when execute=true";
        props["max_results"] = std::move(p_max);

        schema["properties"] = std::move(props);

        json::Value req = json::Value::make_array();
        req.push_back("intent");
        schema["required"] = std::move(req);

        def.input_schema = std::move(schema);
        def.handler = handle_smart_search;
        server.register_tool(std::move(def));
    }

    // --- open tool ---
    {
        mcp::ToolDef def;
        def.name = "open";
        def.description =
            "Open a file or folder with its default application using ShellExecute. "
            "The file must exist. Returns success/failure with error details.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";

        json::Value props = json::Value::make_object();
        json::Value p = json::Value::make_object();
        p["type"] = "string";
        p["description"] = "Absolute path to the file or folder to open";
        props["path"] = std::move(p);
        schema["properties"] = std::move(props);

        json::Value req = json::Value::make_array();
        req.push_back("path");
        schema["required"] = std::move(req);

        def.input_schema = std::move(schema);
        def.handler = handle_open;
        server.register_tool(std::move(def));
    }

    // --- reveal tool ---
    {
        mcp::ToolDef def;
        def.name = "reveal";
        def.description =
            "Reveal a file in Windows Explorer with the item selected. "
            "Uses explorer.exe /select,\"path\". The file must exist.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";

        json::Value props = json::Value::make_object();
        json::Value p = json::Value::make_object();
        p["type"] = "string";
        p["description"] = "Absolute path to the file to reveal in Explorer";
        props["path"] = std::move(p);
        schema["properties"] = std::move(props);

        json::Value req = json::Value::make_array();
        req.push_back("path");
        schema["required"] = std::move(req);

        def.input_schema = std::move(schema);
        def.handler = handle_reveal;
        server.register_tool(std::move(def));
    }

    // --- copy_path tool ---
    {
        mcp::ToolDef def;
        def.name = "copy_path";
        def.description =
            "Copy the absolute file path to the clipboard. "
            "Uses the Windows clipboard API with CF_UNICODETEXT format.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";

        json::Value props = json::Value::make_object();
        json::Value p = json::Value::make_object();
        p["type"] = "string";
        p["description"] = "Absolute path to copy to clipboard";
        props["path"] = std::move(p);
        schema["properties"] = std::move(props);

        json::Value req = json::Value::make_array();
        req.push_back("path");
        schema["required"] = std::move(req);

        def.input_schema = std::move(schema);
        def.handler = handle_copy_path;
        server.register_tool(std::move(def));
    }

    // --- preview tool ---
    {
        mcp::ToolDef def;
        def.name = "preview";
        def.description =
            "Preview a file with type-aware metadata extraction. "
            "Text files: first N lines. PE binaries (exe/dll): version info, company name. "
            "Images (png/jpg/gif/bmp/webp): dimensions from header parsing. "
            "Archives (zip): central directory file listing. "
            "Other files: size, dates, attributes.";

        json::Value schema = json::Value::make_object();
        schema["type"] = "object";

        json::Value props = json::Value::make_object();

        json::Value p_path = json::Value::make_object();
        p_path["type"] = "string";
        p_path["description"] = "Absolute path to the file to preview";
        props["path"] = std::move(p_path);

        json::Value p_lines = json::Value::make_object();
        p_lines["type"] = "integer";
        p_lines["default"] = 100;
        p_lines["minimum"] = 1;
        p_lines["maximum"] = 10000;
        p_lines["description"] = "Maximum lines to return for text files";
        props["max_lines"] = std::move(p_lines);

        schema["properties"] = std::move(props);

        json::Value req = json::Value::make_array();
        req.push_back("path");
        schema["required"] = std::move(req);

        def.input_schema = std::move(schema);
        def.handler = handle_preview;
        server.register_tool(std::move(def));
    }

    // ========================================================
    // MCP Resource Templates
    // ========================================================

    // --- everything://search/{query} ---
    {
        mcp::ResourceTemplateDef tmpl;
        tmpl.uri_template = "everything://search/{query}";
        tmpl.name = "Everything Search";
        tmpl.description = "Execute an Everything search query and return results as JSON";
        tmpl.mime_type = "application/json";
        tmpl.handler = [](const std::string& uri) -> json::Value {
            // Extract query from URI: everything://search/{query}
            std::string prefix = "everything://search/";
            std::string query;
            if (uri.size() > prefix.size()) {
                query = uri.substr(prefix.size());
                query = url_decode(query);
            }

            json::Value result = json::Value::make_object();
            result["uri"] = uri;
            result["query"] = query;

            everything::SearchOptions opts;
            opts.query = query;
            opts.include_size = true;
            opts.include_dates = true;
            opts.max_results = 100;

            auto resp = everything::search(opts);
            result["total_results"] = resp.total_results;
            result["success"] = resp.success;
            if (!resp.success) {
                result["error"] = resp.error_message;
                return result;
            }

            json::Value results_arr = json::Value::make_array();
            for (const auto& r : resp.results) {
                json::Value item = json::Value::make_object();
                item["file_name"] = r.file_name;
                item["full_path"] = r.full_path;
                item["type"] = r.is_folder ? "folder" : "file";
                if (r.size.has_value()) item["size"] = *r.size;
                results_arr.push_back(std::move(item));
            }
            result["results"] = std::move(results_arr);
            return result;
        };
        server.register_resource_template(std::move(tmpl));
    }

    // --- everything://count/{query} ---
    {
        mcp::ResourceTemplateDef tmpl;
        tmpl.uri_template = "everything://count/{query}";
        tmpl.name = "Everything Count";
        tmpl.description = "Get the result count for an Everything query";
        tmpl.mime_type = "application/json";
        tmpl.handler = [](const std::string& uri) -> json::Value {
            std::string prefix = "everything://count/";
            std::string query;
            if (uri.size() > prefix.size()) {
                query = uri.substr(prefix.size());
                query = url_decode(query);
            }

            int count = everything::get_result_count(query);

            json::Value result = json::Value::make_object();
            result["uri"] = uri;
            result["query"] = query;
            result["total_results"] = count;
            return result;
        };
        server.register_resource_template(std::move(tmpl));
    }

    // ========================================================
    // Static Resources (curated)
    // ========================================================

    {
        mcp::StaticResource res;
        res.uri = "everything://search/dupe:";
        res.name = "Duplicate Files";
        res.description = "All files with duplicate names on the system";
        res.handler = [](const std::string& uri) -> json::Value {
            everything::SearchOptions opts;
            opts.query = "dupe:";
            opts.include_size = true;
            opts.include_dates = false;
            opts.max_results = 200;

            auto resp = everything::search(opts);
            json::Value result = json::Value::make_object();
            result["uri"] = uri;
            result["total_results"] = resp.total_results;
            return result;
        };
        server.register_static_resource(std::move(res));
    }

    {
        mcp::StaticResource res;
        res.uri = "everything://count/";
        res.name = "Total Indexed Files";
        res.description = "Total number of files and folders in the Everything index";
        res.handler = [](const std::string& uri) -> json::Value {
            // Use is_running() instead of querying "*" which hangs on Everything 1.5
            json::Value result = json::Value::make_object();
            result["uri"] = uri;
            result["available"] = everything::is_running();
            result["note"] = "Use everything://count/{query} for specific counts";
            return result;
        };
        server.register_static_resource(std::move(res));
    }

    {
        mcp::StaticResource res;
        res.uri = "everything://search/ext:pdf";
        res.name = "All PDF Files";
        res.description = "All PDF files indexed by Everything";
        res.handler = [](const std::string& uri) -> json::Value {
            everything::SearchOptions opts;
            opts.query = "ext:pdf";
            opts.include_size = true;
            opts.include_dates = false;
            opts.max_results = 100;

            auto resp = everything::search(opts);
            json::Value result = json::Value::make_object();
            result["uri"] = uri;
            result["total_results"] = resp.total_results;
            return result;
        };
        server.register_static_resource(std::move(res));
    }
}

} // namespace mcp
