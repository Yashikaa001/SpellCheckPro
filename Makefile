# SpellCheck Pro - Makefile (Linux / macOS / MSYS2 on Windows)
#   make        build build/spellcheck
#   make run    build and start the server
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread -Ibackend/include -Ibackend/third_party
SRC      := $(wildcard backend/src/*.cpp)
OBJ      := $(patsubst backend/src/%.cpp,build/%.o,$(SRC))
ifeq ($(OS),Windows_NT)
  TARGET := build/spellcheck.exe
  LIBS   := -lws2_32
else
  TARGET := build/spellcheck
  LIBS   :=
endif

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

build/%.o: backend/src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

# make test: runs backend/tests/test_core.cpp (everything except the web server)
TEST_SRC := $(filter-out backend/src/main.cpp backend/src/Api.cpp,$(SRC))
build/test_core: backend/tests/test_core.cpp $(TEST_SRC) | build
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

test: build/test_core
	./build/test_core

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf build
.PHONY: run clean test
