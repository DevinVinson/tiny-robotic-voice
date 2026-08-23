CXX ?= clang++
CC ?= clang
AR ?= ar

BUILD_DIR := build
FLITE_DIR := third_party/flite
FLITE_LIB_DIR = $(firstword $(wildcard $(FLITE_DIR)/build/*/lib))

CPPFLAGS := -Iinclude -Ithird_party/yyjson -Ithird_party/miniaudio -I$(FLITE_DIR)/include
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread
CFLAGS := -O2 -Wall -Wextra
LDLIBS = -L$(FLITE_LIB_DIR) -lflite_cmu_us_kal -lflite_usenglish -lflite_cmulex -lflite \
	-framework CoreAudio -framework AudioToolbox -framework CoreFoundation -framework AVFoundation

TRV_CPP := src/main.cpp src/chunker.cpp src/protocol.cpp src/flite_engine.cpp \
	src/miniaudio_output.cpp src/streaming_runtime.cpp src/voice_settings.cpp \
	src/audio_processing.cpp
TRV_OBJECTS := $(TRV_CPP:%.cpp=$(BUILD_DIR)/%.o) \
	$(BUILD_DIR)/src/miniaudio_impl.o $(BUILD_DIR)/third_party/yyjson/yyjson.o

.PHONY: all clean test flite

all: $(BUILD_DIR)/trv

$(BUILD_DIR)/trv: flite $(TRV_OBJECTS)
	$(CXX) $(CXXFLAGS) $(TRV_OBJECTS) $(LDLIBS) -o $@

flite: $(FLITE_DIR)/config/config
	$(MAKE) -C $(FLITE_DIR) -j4

$(FLITE_DIR)/config/config:
	cd $(FLITE_DIR) && ./configure --with-audio=none --with-langvox=ben

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/src/miniaudio_impl.o: src/miniaudio_impl.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/third_party/yyjson/yyjson.o: third_party/yyjson/yyjson.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

TEST_COMMON := $(BUILD_DIR)/src/chunker.o $(BUILD_DIR)/src/protocol.o \
	$(BUILD_DIR)/src/voice_settings.o $(BUILD_DIR)/src/audio_processing.o \
	$(BUILD_DIR)/third_party/yyjson/yyjson.o

$(BUILD_DIR)/tests/test_core: tests/test_core.cpp $(TEST_COMMON)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/tests/test_runtime: tests/test_runtime.cpp $(BUILD_DIR)/src/chunker.o \
	$(BUILD_DIR)/src/protocol.o $(BUILD_DIR)/src/streaming_runtime.o \
	$(BUILD_DIR)/src/voice_settings.o $(BUILD_DIR)/src/audio_processing.o \
	$(BUILD_DIR)/third_party/yyjson/yyjson.o
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/tests/test_flite: tests/test_flite.cpp $(BUILD_DIR)/src/flite_engine.o | flite
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -L$(FLITE_LIB_DIR) \
		-lflite_cmu_us_kal -lflite_usenglish -lflite_cmulex -lflite -o $@

test: flite $(BUILD_DIR)/tests/test_core $(BUILD_DIR)/tests/test_runtime $(BUILD_DIR)/tests/test_flite
	$(BUILD_DIR)/tests/test_core
	$(BUILD_DIR)/tests/test_runtime
	$(BUILD_DIR)/tests/test_flite

clean:
	$(MAKE) -C $(FLITE_DIR) clean || true
	rm -rf $(BUILD_DIR)
