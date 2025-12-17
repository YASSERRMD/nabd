#!/bin/bash
set -e

echo "Building NABD Docker image..."
docker build -t nabd-test .

echo "Running tests in container..."
docker run --rm nabd-test
