CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Iinclude

TARGET := router
SOURCES := src/main.cc src/parser.cc

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

run: $(TARGET)
	./$(TARGET) $(or $(SAMPLE),samples/sample0.txt)

clean:
	rm -f $(TARGET)
