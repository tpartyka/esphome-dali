# Host unit tests for the DALI protocol core (components/dali/dali.h,
# dali_bus_manager.cpp, dali_tx_collision.h). Builds and runs as a plain
# host binary, no ESPHome/Arduino/ESP-IDF needed.
#
# Usage:
#   make            # build and run
#   make run        # same
#   make clean

CXX ?= c++
# -DARDUINO=1: components/dali/port.h includes <esphome.h> unconditionally and
# then branches on ARDUINO. host_shims/esphome.h is a stub providing just
# delay()/LOW/HIGH for the ARDUINO branch (delayMilliseconds = delay()).
CXXFLAGS ?= -std=c++17 -Wall -Wextra -g -DARDUINO=1

DALI_DIR := ../components/dali

# host_shims must come first so <esphome.h> resolves to our stub.
INCLUDES := -I host_shims -I $(DALI_DIR) -I .

SOURCES := test_main.cpp \
           dali_port_base.cpp \
           test_commands.cpp \
           test_lamp.cpp \
           test_groups_scenes.cpp \
           test_color_led.cpp \
           test_bus_manager.cpp \
           test_addressing.cpp \
           test_collision.cpp \
           test_debounce.cpp \
           test_availability.cpp \
           test_bus_health.cpp \
           test_inventory.cpp \
           test_names.cpp \
           test_event_log.cpp \
           test_circadian.cpp \
           $(DALI_DIR)/dali_bus_manager.cpp

BIN := dali_tests

.PHONY: all run clean

all: run

$(BIN): $(SOURCES) framework.h mock_port.h virtual_bus.h
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) -o $(BIN)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
