#pragma once

#include <memory>
#include <string>
#include <vector>
#include "query/query_context.h"

namespace CustomOps {

class CustomPlanner;
class TwoColumnStore;

// HTTP handler for custom query execution
class CustomQueryHandler {
private:
    QueryContext& query_context;
    std::unique_ptr<CustomPlanner> planner;
    std::unique_ptr<TwoColumnStore> result_store;

public:
    CustomQueryHandler(QueryContext& ctx);
    ~CustomQueryHandler();

    // Process custom query request
    struct QueryRequest {
        std::string query;
        std::string format; // "json", "csv", "binary"
        bool optimize;
        bool explain;
        size_t limit;
    };

    struct QueryResponse {
        bool success;
        std::string error_message;
        std::vector<std::vector<std::string>> results;
        std::string plan;
        uint64_t execution_time_ms;
        uint64_t rows_returned;
    };

    QueryResponse execute(const QueryRequest& request);

    // Configuration
    void set_memory_limit(size_t mb);
    void enable_optimization(bool enable);
    void enable_caching(bool enable);
};

// REST API endpoints for custom queries
class CustomQueryAPI {
public:
    // POST /custom/query - Execute a custom query
    static void handle_query(const std::string& request_body, std::string& response_body);

    // GET /custom/plan?query=... - Get query plan without execution
    static void handle_explain(const std::string& query, std::string& response_body);

    // GET /custom/stats - Get statistics about custom query execution
    static void handle_stats(std::string& response_body);

    // POST /custom/config - Update configuration
    static void handle_config(const std::string& request_body, std::string& response_body);
};

} // namespace CustomOps