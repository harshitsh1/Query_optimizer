# Simple Query Optimizer

A lightweight SQL Query Optimizer built in C++ that demonstrates the foundational concepts of relational database query optimization. It features a SQL parser, heuristic logical rewrites, a dynamic-programming-based cost-enumerator (similar to System R), and a built-in web UI to visualize query plans before and after optimization.

## Features

- **SQL Parser**: Utilizes **Flex & Bison** to parse a subset of SQL (`SELECT`, `FROM`, `JOIN ... ON`, `WHERE` conditions).
- **Heuristic Optimization**: 
  - Predicate / Selection Pushdown
  - Inner Join Reordering (moving the smallest table to the left outer relation)
- **Cost-Based Query Plan Enumeration**:
  - Dynamically builds physical execution plans.
  - Cost-Estimates sequential scans vs. various index scans.
  - Cost-Estimates nested loop, block nested loop, hash join, and merge-sort joins.
- **Interactive Web Interface**: A custom C++ HTTP server hosts a web application layout that allows you to interactively type SQL and instantly compare the **Unoptimized Physical Plan** against the **Optimized Physical Plan** alongside side-by-side cost percentage drops.
- **Mock Catalog**: Plugs into an in-memory University DB catalog dataset giving metadata attributes such as tuple counts, distinct key counts and indexes (B+Tree, Hash) to inform cost strategies.

## Prerequisites

- **C++17 Compiler** (e.g., `g++` via MinGW on Windows)
- **Make**
- **Flex** and **Bison**
- **Windows Networking Lib** (`ws2_32` for the HTTP server socket bindings)

## Build Instructions

Using the provided Makefile, you can compile the application easily from the root directory:

```bash
make clean
make
```

This will link and generate the `query_optimizer.exe` executable.

## Usage

You can run the optimizer in several ways depending on your needs.

### 1. Web Visualization Mode (Recommended)
Launch the interactive web UI! The system will boot an HTTP server locally and serve interactive HTML, CSS, and JS components to trace the DB trees.

```bash
./query_optimizer.exe --server 8080
```
Then navigate to `http://localhost:8080` in your web browser. Type in your SQL, click "Optimize", and view the generated plans and cost graphs in real time!

### 2. Interactive CLI Mode
Provide SQL queries line-by-line interactively via your terminal:

```bash
./query_optimizer.exe
```

### 3. Single File Mode
Parse and optimize a single SQL file containing one overarching query:

```bash
./query_optimizer.exe test_query.sql
```

### 4. Batch Processing Mode
Optimize multiple queries simultaneously from a single file (queries should be separated by blank lines or semicolons). Output features robust benchmarking.

```bash
./query_optimizer.exe --batch test_batch.sql
```

## Directory Structure

- `src/` - Source code for core backend (Cost Estimator, DP Plan Enumeration, Heuristic Rule-sets).
- `include/` - Core header structures and prototypes.
- `web/` - Frontend assets (`index.html`, `app.js`, `style.css`).
- `sql_lexer.l` & `sql_parser.y` - Flex and Bison files for understanding SQL inputs.
- `Makefile` - Project build instructions.
