CXX = clang++
CXXFLAGS = -std=c++17 -pthread
SRCS = main.cpp banks.cpp market.cpp shock.cpp contagion.cpp output.cpp benchmarking.cpp small_market.cpp
TARGET = contagion_model

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
