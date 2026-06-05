
CXX      := g++
CXXFLAGS := -std=c++20   -O2 -g

TARGET   := mkfs.minibtrfs

SRCS     := mkfs.minibtrfs.cc btree.cpp
OBJS     := $(SRCS:.cc=.o)
OBJS     := $(OBJS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) *.o




.PHONY: all clean 