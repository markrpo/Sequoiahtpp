SRCS=$(wildcard *.cpp)
OBJ=$(SRCS:.cpp=.o)
# DEPS is used to track dependencies for header files
# when a header file changes, the corresponding .o file will be rebuilt
DEPS=$(SRCS:.cpp=.d)
EXE=HTTPServer
CXX=g++

CXXFLAGS=-std=c++17 -Wall -I. -I./interfaces
LDFLAGS=

.DEFAULT_GOAL=all

all: $(EXE)

$(EXE): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^

-include $(DEPS)

# -MMD generates dependency files (.d) for each source file
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

clean:
	rm -f $(OBJ) $(DEPS) $(EXE)

.PHONY: all clean
