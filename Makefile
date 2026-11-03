CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Isrc
COMMON = src/model.cpp src/solver.cpp src/simulator.cpp
HEADERS = src/model.hpp src/solver.hpp src/simulator.hpp

.PHONY: all test clean
all: queue_control build/tests

queue_control: src/main.cpp $(COMMON) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) src/main.cpp $(COMMON) -o $@

build/tests: tests/tests.cpp $(COMMON) $(HEADERS)
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) tests/tests.cpp $(COMMON) -o $@

test: all
	./build/tests deterministic
	./build/tests statistical
	python3 tests/cli_tests.py ./queue_control

clean:
	rm -rf build queue_control
