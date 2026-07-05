CXX      := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -g
LDFLAGS  := -lxcb -lxcb-util -lxcb-keysyms

TARGET   := wm

SRC_DIR  := src
BUILD_DIR := build

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	Xephyr -screen 1280x800 :2 &
	sleep 1
	DISPLAY=:2 ./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
