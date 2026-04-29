#pragma once

#include "planner_metrics.h"
#include <fstream>
#include <iostream>
#include <iomanip>

/**
 * Reports planner comparison metrics in various formats.
 */
class PlannerMetricsReporter {
public:
    // Print comparison summary to console
    static void print_comparison(const std::vector<PlannerMetrics>& metrics, std::ostream& os = std::cout) {
        if (metrics.empty()) {
            os << "No planner metrics collected\n";
            return;
        }

        os << "\n=== Planner Comparison Report ===\n";

        for (size_t i = 0; i < metrics.size(); ++i) {
            const auto& m = metrics[i];
            os << "\nPlanner: " << m.planner_type << "\n";
            os << "  Planning time: " << std::fixed << std::setprecision(3) << m.planning_time_ms << " ms\n";
            os << "  Execution time: " << m.execution_time_ms << " ms\n";
            os << "  Total time: " << m.get_total_time_ms() << " ms\n";
            os << "  Result count: " << m.result_count << "\n";
            os << "  Estimated cost: " << m.estimated_cost << "\n";

            if (m.had_error) {
                os << "  ERROR: " << m.error_message << "\n";
            }

            if (!m.plan_description.empty()) {
                os << "  Plan: " << m.plan_description << "\n";
            }
        }

        // Print comparison if we have multiple planners
        if (metrics.size() == 2) {
            const auto& original = metrics[0].planner_type == "original" ? metrics[0] : metrics[1];
            const auto& custom = metrics[0].planner_type == "custom" ? metrics[0] : metrics[1];

            os << "\n=== Comparison Summary ===\n";
            if (!original.had_error && !custom.had_error) {
                double speedup = original.get_total_time_ms() / custom.get_total_time_ms();
                os << "Speed ratio (original/custom): " << std::fixed << std::setprecision(3) << speedup;
                if (speedup > 1.0) {
                    os << " (custom is " << (speedup - 1.0) * 100 << "% faster)";
                } else {
                    os << " (original is " << (1.0/speedup - 1.0) * 100 << "% faster)";
                }
                os << "\n";

                if (original.result_count == custom.result_count) {
                    os << "Results identical: YES (" << original.result_count << " rows)\n";
                } else {
                    os << "Results identical: NO (original: " << original.result_count
                       << ", custom: " << custom.result_count << ")\n";
                }
            }
        }
        os << "================================\n\n";
    }

    // Write metrics to CSV file
    static void write_csv(const std::vector<PlannerMetrics>& metrics, const std::string& filename) {
        if (metrics.empty()) return;

        std::ofstream file(filename, std::ios::app);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open metrics file: " << filename << std::endl;
            return;
        }

        // Check if file is empty to write header
        file.seekp(0, std::ios::end);
        bool is_empty = file.tellp() == 0;

        if (is_empty) {
            file << "timestamp,planner_type,planning_time_ms,execution_time_ms,total_time_ms,"
                 << "result_count,estimated_cost,num_triple_patterns,num_path_patterns,"
                 << "had_error,error_message,plan_description\n";
        }

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        for (const auto& m : metrics) {
            file << time_t << ","
                 << m.planner_type << ","
                 << m.planning_time_ms << ","
                 << m.execution_time_ms << ","
                 << m.get_total_time_ms() << ","
                 << m.result_count << ","
                 << m.estimated_cost << ","
                 << m.num_triple_patterns << ","
                 << m.num_path_patterns << ","
                 << (m.had_error ? "true" : "false") << ","
                 << "\"" << m.error_message << "\","
                 << "\"" << m.plan_description << "\"\n";
        }

        file.flush();
    }

    // Write detailed JSON report
    static void write_json(const std::vector<PlannerMetrics>& metrics, const std::string& filename) {
        if (metrics.empty()) return;

        std::ofstream file(filename, std::ios::app);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open metrics file: " << filename << std::endl;
            return;
        }

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        file << "{\n";
        file << "  \"timestamp\": " << time_t << ",\n";
        file << "  \"planners\": [\n";

        for (size_t i = 0; i < metrics.size(); ++i) {
            const auto& m = metrics[i];
            file << "    {\n";
            file << "      \"type\": \"" << m.planner_type << "\",\n";
            file << "      \"planning_time_ms\": " << m.planning_time_ms << ",\n";
            file << "      \"execution_time_ms\": " << m.execution_time_ms << ",\n";
            file << "      \"total_time_ms\": " << m.get_total_time_ms() << ",\n";
            file << "      \"result_count\": " << m.result_count << ",\n";
            file << "      \"estimated_cost\": " << m.estimated_cost << ",\n";
            file << "      \"num_triple_patterns\": " << m.num_triple_patterns << ",\n";
            file << "      \"num_path_patterns\": " << m.num_path_patterns << ",\n";
            file << "      \"had_error\": " << (m.had_error ? "true" : "false") << ",\n";
            file << "      \"error_message\": \"" << m.error_message << "\",\n";
            file << "      \"plan_description\": \"" << m.plan_description << "\"\n";
            file << "    }";
            if (i < metrics.size() - 1) file << ",";
            file << "\n";
        }

        file << "  ]\n";
        file << "},\n";
        file.flush();
    }
};