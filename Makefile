# NABD Makefile
#
# Copyright (c) 2025 Mohamed Yasser
# Licensed under MIT License

CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 -D_GNU_SOURCE

# macOS doesn't need/have -lrt, Linux does
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDFLAGS = -lrt -pthread
else
    LDFLAGS = -pthread
endif

# Directories
SRC_DIR = src
INC_DIR = include
EXAMPLES_DIR = examples
BENCH_DIR = benchmarks
BUILD_DIR = build

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Library
LIB = $(BUILD_DIR)/libnabd.a

# Examples
EXAMPLES = $(BUILD_DIR)/simple_producer $(BUILD_DIR)/simple_consumer

# Benchmarks
BENCHMARKS = $(BUILD_DIR)/latency_bench

.PHONY: all clean examples bench test

all: $(LIB) examples bench

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

# Create static library
$(LIB): $(OBJS)
	ar rcs $@ $^

# Examples
examples: $(EXAMPLES)

$(BUILD_DIR)/simple_producer: $(EXAMPLES_DIR)/simple_producer.c $(LIB)
	$(CC) $(CFLAGS) -I$(INC_DIR) $< $(LIB) $(LDFLAGS) -o $@

$(BUILD_DIR)/simple_consumer: $(EXAMPLES_DIR)/simple_consumer.c $(LIB)
	$(CC) $(CFLAGS) -I$(INC_DIR) $< $(LIB) $(LDFLAGS) -o $@

# Benchmarks
bench: $(BENCHMARKS)

$(BUILD_DIR)/latency_bench: $(BENCH_DIR)/latency_bench.c $(LIB)
	$(CC) $(CFLAGS) -I$(INC_DIR) $< $(LIB) $(LDFLAGS) -o $@

# Run examples
run-producer: $(BUILD_DIR)/simple_producer
	./$(BUILD_DIR)/simple_producer

run-consumer: $(BUILD_DIR)/simple_consumer
	./$(BUILD_DIR)/simple_consumer

# Run benchmark
run-bench: $(BUILD_DIR)/latency_bench
	./$(BUILD_DIR)/latency_bench

# Clean
clean:
	rm -rf $(BUILD_DIR)

# Show help
help:
	@echo "NABD Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all           - Build library, examples, and benchmarks"
	@echo "  examples      - Build example programs"
	@echo "  bench         - Build benchmarks"
	@echo "  run-producer  - Run producer example"
	@echo "  run-consumer  - Run consumer example"
	@echo "  run-bench     - Run latency benchmark"
	@echo "  clean         - Remove build artifacts"
