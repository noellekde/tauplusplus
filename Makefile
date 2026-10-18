# Where the Tau (Python edition) repository is checked out; the tests compare against its tau.py
TAU_REPO ?= ../Tau

# Native command-line tau++ (for testing on Linux/macOS/MinGW shells)
build/tau++: tauvm.cpp tauvm.hpp tauxx.cpp
	mkdir -p build
	g++ -std=c++17 -O2 -Wall -Wextra -o build/tau++ tauvm.cpp tauxx.cpp

test: build/tau++
	python3 tests/run_tests.py build/tau++ $(TAU_REPO)

clean:
	rm -rf build
