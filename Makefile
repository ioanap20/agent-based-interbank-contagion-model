CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread
SRCS = main.cpp banks.cpp market.cpp shock.cpp contagion.cpp output.cpp benchmarking.cpp
TARGET = contagion_model

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
