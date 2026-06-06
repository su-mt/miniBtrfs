CXX := g++
CXXFLAGS := -std=c++20 -O2 -g 

BUILD_DIR := build

MAIN_TARGET := main
MKFS_TARGET := mkfs.minibtrfs

COMMON_SRCS := btree.cpp minibtrfs.cpp

MAIN_SRCS := main.cc $(COMMON_SRCS)
MKFS_SRCS := mkfs.minibtrfs.cc $(COMMON_SRCS)

MAIN_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(filter %.cpp,$(MAIN_SRCS))) \
             $(patsubst %.cc,$(BUILD_DIR)/%.o,$(filter %.cc,$(MAIN_SRCS)))

MKFS_OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(filter %.cpp,$(MKFS_SRCS))) \
             $(patsubst %.cc,$(BUILD_DIR)/%.o,$(filter %.cc,$(MKFS_SRCS)))

all: $(MAIN_TARGET)

mkfs: $(MKFS_TARGET)

$(MAIN_TARGET): $(MAIN_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(MKFS_TARGET): $(MKFS_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.cc | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(MAIN_TARGET) $(MKFS_TARGET)

fs:
	rm fs || true
	touch fs
	./mkfs.minibtrfs fs 40960

.PHONY: all mkfs clean