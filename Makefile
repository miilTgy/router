CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O3 -Iinclude

TARGET := router
SOURCES := src/main.cc src/parser.cc src/initializer.cc src/router.cc src/rrr.cc src/writer.cc

EVAL_TARGET := evaluate
EVAL_SOURCE := evaluate.cpp

.PHONY: all run eval clean

ifeq ($(filter eval,$(MAKECMDGOALS)),eval)
ifndef S
$(error Usage: make eval S=<sample_path> R=<solution_path>)
endif
ifndef R
$(error Usage: make eval S=<sample_path> R=<solution_path>)
endif
endif

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

$(EVAL_TARGET): $(EVAL_SOURCE)
	$(CXX) $(CXXFLAGS) $(EVAL_SOURCE) -o $(EVAL_TARGET)

run: $(TARGET)
	./$(TARGET) $(or $(SAMPLE),samples/sample0.txt)

eval: $(EVAL_TARGET)
	./$(EVAL_TARGET) $(S) $(R)

clean:
	rm -f $(TARGET) $(EVAL_TARGET)
