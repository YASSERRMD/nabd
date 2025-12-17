# NABD Test & Build Environment
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    make \
    gcc \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source
COPY . .

# Build library and tests
RUN make clean && make && make test

# Default command runs tests
CMD ["make", "run-test"]
