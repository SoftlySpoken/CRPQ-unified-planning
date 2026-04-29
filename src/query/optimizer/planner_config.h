#pragma once

#include <string>

/**
 * Global configuration for query planners.
 * This configuration is set at server startup and used throughout
 * the query execution pipeline to determine planner behavior.
 */
struct PlannerConfig {
    // Whether custom planner is enabled
    bool custom_planner_enabled = false;

    // Whether to run both planners for comparison
    bool custom_planner_compare = false;

    // Whether to enable verbose logging for planner operations
    bool custom_planner_verbose = false;

    // Output file for comparison metrics (empty means no output)
    std::string custom_planner_output_file;

    // Get the global planner configuration
    static PlannerConfig& get_instance() {
        static PlannerConfig instance;
        return instance;
    }

    // Set the configuration from command line arguments
    static void configure(bool enabled, bool compare, bool verbose, const std::string& output_file) {
        auto& config = get_instance();
        config.custom_planner_enabled = enabled;
        config.custom_planner_compare = compare;
        config.custom_planner_verbose = verbose;
        config.custom_planner_output_file = output_file;
    }

    // Check if custom planner should be used
    bool should_use_custom_planner() const {
        return custom_planner_enabled || custom_planner_compare;
    }

    // Check if comparison mode is enabled
    bool should_compare_planners() const {
        return custom_planner_compare;
    }

    // Check if verbose output is enabled
    bool is_verbose() const {
        return custom_planner_verbose;
    }

    // Get output file path (empty if not configured)
    const std::string& get_output_file() const {
        return custom_planner_output_file;
    }
};