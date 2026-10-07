CXX ?= g++
FLAGS = -Wall -Wextra -Werror -std=c++17 -pedantic -O2
INCLUDES = -Isrc

all: build/test_dbc build/decode_demo

build/test_dbc: src/dbc_parse.cpp src/dbc_decode.cpp tests/test_dbc.cpp
	mkdir -p build
	$(CXX) $(FLAGS) $(INCLUDES) -o $@ $^

build/decode_demo: src/dbc_parse.cpp src/dbc_decode.cpp demo/decode_demo.cpp
	mkdir -p build
	$(CXX) $(FLAGS) $(INCLUDES) -o $@ $^

test: all
	./build/test_dbc

clean:
	rm -rf build
