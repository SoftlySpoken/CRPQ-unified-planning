# MillenniumDB with Unified CRPQ Query Planner

MillenniumDB is a graph-oriented database management system developed by the [Millennium Institute for Foundational Research on Data (IMFD)](https://imfd.cl/). This version builds on its basis a novel custom query planner specifically designed for optimizing conjunctive regular path queries (CRPQs), accompanying the paper: Yue Pang, Lei Zou, Angela Bonifati, M. Tamer Özsu, Xiaofang Zhou, "A Unified Query Planning Framework for Conjunctive Regular Path Queries." Please refer to the included research paper (`full_paper.pdf`) for technical details and experimental results.

## Table of Contents
- [Main Features](#main-features)
- [Installation and Compilation](#installation-and-compilation)
- [Running the System](#running-the-system)
- [Testing Framework](#testing-framework)
- [Visualization and Analysis](#visualization-and-analysis)

## Main Features

### Custom Query Planner
- **Advanced CRPQ Support**: Optimized execution for conjunctive regular path queries
- **Custom Operators**: Specialized operators (KC, MC, SJ, TI, Union) for efficient query execution
- **Performance Analytics**: Comprehensive metrics collection and reporting

### Custom Operators
- **SJ (Subgraph Join) Operator**: Emulates relational-style join for query subgraph merging
- **TI (Traverse-Intersect) Operator**: Traversal-style evaluation for RPQs and their conjunction
- **KC (Kleene Closure) Operator**: Optimized for Kleene closure queries
- **Union Operator**: Union operations for path alternatives

### Testing and Benchmarking
- **Correctness Driver**: Large-scale query correctness verification
- **Performance Visualization**: Box plots, curve plots, and LaTeX tables
- **Unit Testing**: Comprehensive test suite for all custom components

## Installation and Compilation

MillenniumDB should be able to be built on any x86-64 Linux distribution. On Windows, Windows Subsystem for Linux (WSL) can be used.

### Install Dependencies

MillenniumDB needs the following dependencies:
- GCC >= 8.1
- CMake >= 3.12
- Git
- libssl
- ncursesw and less for the CLI
- Python >= 3.8 with venv to run tests

On current Debian and Ubuntu based distributions they can be installed by running:
```bash
sudo apt update && sudo apt install git g++ cmake libssl-dev libncurses-dev locales less python3 python3-venv libomp-dev
```

The `en_US.UTF-8` locale also needs to be generated. On Ubuntu based distributions this can be done as follows:
```bash
sudo locale-gen en_US.UTF-8
```

### Clone the Repository

Clone this repository, enter the repository root directory and set `MDB_HOME`:
```bash
git clone git@github.com:MillenniumDB/MillenniumDB-PreRelease.git
cd MillenniumDB-PreRelease
export MDB_HOME=$(pwd)
```

### Install Boost

Download [`boost_1_82_0.tar.gz`](https://boostorg.jfrog.io/artifactory/main/release/1.82.0/source/boost_1_82_0.tar.gz) using a browser or wget:
```bash
wget -q --show-progress https://boostorg.jfrog.io/artifactory/main/release/1.82.0/source/boost_1_82_0.tar.gz
```

and run the following in the directory where boost was downloaded:
```bash
tar -xf boost_1_82_0.tar.gz
mkdir -p $MDB_HOME/third_party/boost_1_82/include
mv boost_1_82_0/boost $MDB_HOME/third_party/boost_1_82/include
rm -r boost_1_82_0.tar.gz boost_1_82_0
```

### Build Options

```bash
# Release build (recommended for production)
cmake -B build/Release -D CMAKE_BUILD_TYPE=Release
cmake --build build/Release/ -j$(nproc)

# Debug build (for development)
cmake -B build/Debug -D CMAKE_BUILD_TYPE=Debug
cmake --build build/Debug/ -j$(nproc)

# Sanitize build (for memory error detection with AddressSanitizer and UndefinedBehaviorSanitizer)
cmake -B build/Sanitize -D CMAKE_BUILD_TYPE=Sanitize
cmake --build build/Sanitize/ -j$(nproc)
```

### Build Targets
The build process creates several binaries:
- `mdb-server`: Main database server
- `mdb-import`: Database creation from RDF files
- `mdb-cli`: Command-line interface
- `mdb-custom-query`: Custom planner query executor
- `mdb-dump`: Database export utility

## Running the System

### Creating a Database
```bash
# Create database from Turtle file
build/Release/bin/mdb-import <data-file> <db-directory> [--prefixes <prefixes-file>]
```
- `<data-file>` is the path to the file containing the data to import, in the [Turtle](https://www.w3.org/TR/turtle/) format.
- `<db-directory>` is the path of the directory where the new database will be created.
- `--prefixes <prefixes-file>` is an optional path to a prefixes file.

In the paper's experiments, Wikidata is used as the dataset as specified by WDBench, which can be downloaded [here](https://disk.pku.edu.cn/link/AA53ABC8A0878D484FB5593320B0D59C03).

### Server Operations

#### Run the Server
```bash
# Basic server startup
build/Release/bin/mdb-server <db-directory>

# Server with custom planner setup
build/Release/bin/mdb-server <db-directory> --custom-planner --custom-planner-verbose
```

### Execute Queries

#### SPARQL Protocol
The server supports all SPARQL 1.1 Protocol operations:
```bash
# Query via POST
curl -X POST http://localhost:8080/sparql \
     -H "Content-Type: application/sparql-query" \
     -d "SELECT ?s ?p ?o WHERE { ?s ?p ?o } LIMIT 10"
```

#### Python Script
Install and use the provided Python script:
```bash
pip3 install sparqlwrapper
python3 scripts/sparql_query.py <query-file>
```

## Testing Framework

### Unit Tests
```bash
# Run all unit tests
./scripts/run-unit-tests

# Run specific test categories
./scripts/run-unit-tests --gtest_filter="CustomPlanner*"
./scripts/run-unit-tests --gtest_filter="CustomOps*"
```

### Correctness Testing
Large-scale correctness verification for query benchmarks:

```bash
# Basic correctness testing
cd scripts/correctness/
python3 correctness_driver.py \
    --queries /path/to/query/file.txt \
    --base-timeout 1000 \
    --script-timeout 36000 \
    --output-path ./results \
    --build-type Debug \
    --verbose

# Using existing reference results
python3 correctness_driver.py \
    --queries /path/to/query/file.txt \
    --base-timeout 1000 \
    --script-timeout 36000 \
    --output-path ./results \
    --build-type Debug \
    --original-result ./results/original_results.json \
    --verbose
```

### Performance Scripts
```bash
# Run queries with timeout measurement
python3 run_queries_with_timeout.py \
    --queries query_file.txt \
    --db database_directory/ \
    --timeout 36000 \
    --build-type Release \
    --custom \
    --output results.json

# Compare results between planners
python3 scripts/correctness/compare_results.py original_results.json custom_results.json
```

The query file used in the paper's experiments is derived from [the CRPQ workload of WDBench](https://github.com/MillenniumDB/WDBench/blob/master/Queries/c2rpqs.txt). On its basis, we decompose each query graph into its connected components and treating each component as an independent query. This is because the conjunction between disconnected components corresponds to a Cartesian product, which cannot be optimized using logical query transformations and thus falls outside the scope of our proposed algebra. As a result of this preprocessing step, the original set of 539 queries expands to 551. The query file that we use is `c2rpqs-connected.txt`.

## Visualization and Analysis

### Generate Performance Plots and Tables
```bash
# Generate all visualizations (tables, box plots, curve plots)
cd scripts/plots/
./gen_all_plots_tables.sh original_results.json custom_results.json

# Individual plot generation
python3 table/get_table.py original_results.json custom_results.json output.tex
python3 box_plot/box_plot_durations.py original_results.json custom_results.json output_dir/
python3 curve_plot/curve_plot.py query_file.txt original_results.json custom_results.json --output output_dir/
```

### Plot Types
- **LaTeX Tables**: Formatted performance comparison tables
- **Box Plots**: Distribution visualization of query execution times
- **Curve Plots**: Performance curves across different query complexities
