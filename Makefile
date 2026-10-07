CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

TARGETS = oss worker

all: $(TARGETS)

oss: oss.cpp
	$(CXX) $(CXXFLAGS) -o oss oss.cpp

worker: worker.cpp
	$(CXX) $(CXXFLAGS) -o worker worker.cpp

clean:
	rm -f $(TARGETS)

.PHONY: all clean
