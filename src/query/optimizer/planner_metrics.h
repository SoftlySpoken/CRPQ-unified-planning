#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

/**
 * Metrics collected during query planning and execution for performance comparison.
 */
struct PlannerMetrics {
    // Planning phase metrics
    std::chrono::high_resolution_clock::time_point planning_start;
    std::chrono::high_resolution_clock::time_point planning_end;
    double planning_time_ms = 0.0;

    // Plan characteristics
    std::string planner_type; // "original" or "custom"
    size_t num_triple_patterns = 0;
    size_t num_path_patterns = 0;
    double estimated_cost = 0.0;
    std::string plan_description;

    // Execution metrics
    std::chrono::high_resolution_clock::time_point execution_start;
    std::chrono::high_resolution_clock::time_point execution_end;
    double execution_time_ms = 0.0;
    uint64_t result_count = 0;
    size_t peak_memory_usage = 0;

    // Error information
    bool had_error = false;
    std::string error_message;

    // Start timing planning phase
    void start_planning(const std::string& type) {
        planner_type = type;
        planning_start = std::chrono::high_resolution_clock::now();
    }

    // End timing planning phase
    void end_planning() {
        planning_end = std::chrono::high_resolution_clock::now();
        planning_time_ms = std::chrono::duration<double, std::milli>(planning_end - planning_start).count();
    }

    // Start timing execution phase
    void start_execution() {
        execution_start = std::chrono::high_resolution_clock::now();
    }

    // End timing execution phase
    void end_execution() {
        execution_end = std::chrono::high_resolution_clock::now();
        execution_time_ms = std::chrono::duration<double, std::milli>(execution_end - execution_start).count();
    }

    // Set error information
    void set_error(const std::string& message) {
        had_error = true;
        error_message = message;
    }

    // Get total time (planning + execution)
    double get_total_time_ms() const {
        return planning_time_ms + execution_time_ms;
    }
};

/**
 * Collects and manages metrics for planner comparison.
 * Thread-local to handle concurrent queries.
 */
class PlannerMetricsCollector {
private:
    std::vector<PlannerMetrics> metrics;
    bool collecting_enabled = false;

public:
    // Enable/disable metrics collection
    void set_enabled(bool enabled) {
        collecting_enabled = enabled;
    }

    bool is_enabled() const {
        return collecting_enabled;
    }

    // Start collecting metrics for a new query
    void start_query() {
        if (collecting_enabled) {
            metrics.clear();
        }
    }

    // Add metrics for a planner execution
    void add_metrics(const PlannerMetrics& metric) {
        if (collecting_enabled) {
            metrics.push_back(metric);
        }
    }

    // Get all collected metrics for the current query
    const std::vector<PlannerMetrics>& get_metrics() const {
        return metrics;
    }

    // Clear collected metrics
    void clear() {
        metrics.clear();
    }

    // Get singleton instance (thread-local)
    static PlannerMetricsCollector& get_instance() {
        thread_local PlannerMetricsCollector instance;
        return instance;
    }
};

/**
 * RAII helper for timing planner operations.
 */
class PlannerTimer {
private:
    PlannerMetrics* metrics_;
    bool is_execution_;

public:
    PlannerTimer(PlannerMetrics* metrics, bool is_execution = false)
        : metrics_(metrics), is_execution_(is_execution) {
        if (metrics_) {
            if (is_execution_) {
                metrics_->start_execution();
            } else {
                metrics_->start_planning(metrics_->planner_type);
            }
        }
    }

    ~PlannerTimer() {
        if (metrics_) {
            if (is_execution_) {
                metrics_->end_execution();
            } else {
                metrics_->end_planning();
            }
        }
    }
};