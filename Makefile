# FlockSquawk — arduino-cli build system
# Usage: make help

# ──────────────────────────────────────────────
# Dependency versions (shared with Dockerfile)
# ──────────────────────────────────────────────
include versions.env

# ──────────────────────────────────────────────
# User-configurable variables
# ──────────────────────────────────────────────
PORT         ?=
BAUD         ?= 115200
VARIANT      ?= m5stick
CORE_VERSION ?= $(ESP32_CORE_VERSION)

BUILD_DIR    := $(CURDIR)/.build
COMMON_DIR   := $(CURDIR)/common

# Arduino toolchain paths (Linux default; macOS overrides below)
ARDUINO_DATA  ?= $(HOME)/.arduino15
ifeq ($(shell uname),Darwin)
  ARDUINO_DATA := $(HOME)/Library/Arduino15
endif

ESPTOOL       = $(firstword $(wildcard $(ARDUINO_DATA)/packages/esp32/tools/esptool_py/*/esptool.py) \
                $(wildcard $(ARDUINO_DATA)/packages/esp32/tools/esptool_py/*/esptool))
MKLITTLEFS    = $(firstword $(wildcard $(ARDUINO_DATA)/packages/esp32/tools/mklittlefs/*/mklittlefs))

# ──────────────────────────────────────────────
# Variant definitions:  NAME  FQBN  SKETCH_DIR  HAS_DATA
# ──────────────────────────────────────────────
VARIANTS := m5stick m5fire mini12864 oled portable flipper

m5stick_FQBN   := esp32:esp32:m5stack_stickc_plus2
m5stick_SKETCH := m5stack/flocksquawk_m5stick
m5stick_DATA   :=

m5fire_FQBN    := esp32:esp32:m5stack_fire
m5fire_SKETCH  := m5stack/flocksquawk_m5fire
m5fire_DATA    := 1

mini12864_FQBN   := esp32:esp32:esp32
mini12864_SKETCH := Mini12864/flocksquawk_mini12864
mini12864_DATA   := 1

oled_FQBN   := esp32:esp32:esp32
oled_SKETCH := 128x32_OLED/flocksquawk_128x32
oled_DATA   := 1

portable_FQBN   := esp32:esp32:esp32
portable_SKETCH := 128x32_OLED/flocksquawk_128x32_portable
portable_DATA   :=

flipper_FQBN   := esp32:esp32:esp32s2
flipper_SKETCH := flipper-zero/dev-board-firmware/flocksquawk-flipper
flipper_DATA   :=

# LittleFS defaults (4 MB flash, default partition table)
LITTLEFS_OFFSET ?= 0x290000
LITTLEFS_SIZE   ?= 0x160000
LITTLEFS_PAGE   ?= 256
LITTLEFS_BLOCK  ?= 4096

# ──────────────────────────────────────────────
# Auto-detect serial port when PORT is empty
# ──────────────────────────────────────────────
define detect_port
$(or $(PORT),$(firstword $(wildcard /dev/ttyUSB* /dev/ttyACM* /dev/cu.usbserial* /dev/cu.usbmodem* /dev/cu.wchusbserial*)))
endef

# ──────────────────────────────────────────────
# Per-variant target template
# ──────────────────────────────────────────────
define VARIANT_TARGETS

.PHONY: build-$(1) upload-$(1) flash-$(1) monitor-$(1)

build-$(1):
	arduino-cli compile \
		--fqbn $($(1)_FQBN) \
		--build-property "build.defines=-I$(COMMON_DIR)" \
		--output-dir $(BUILD_DIR)/$(1) \
		$($(1)_SKETCH)

upload-$(1):
	$$(if $$(call detect_port),, $$(error PORT not set and no device auto-detected))
	arduino-cli upload \
		--fqbn $($(1)_FQBN) \
		--port $$(call detect_port) \
		--input-dir $(BUILD_DIR)/$(1) \
		$($(1)_SKETCH)

flash-$(1): build-$(1) upload-$(1)

monitor-$(1):
	$$(if $$(call detect_port),, $$(error PORT not set and no device auto-detected))
	arduino-cli monitor \
		--port $$(call detect_port) \
		--config baudrate=$(BAUD)

# Only generate upload-data target for variants with a data/ directory
ifneq ($($(1)_DATA),)
.PHONY: upload-data-$(1)
upload-data-$(1):
	$$(if $$(call detect_port),, $$(error PORT not set and no device auto-detected))
	$$(if $(MKLITTLEFS),, $$(error mklittlefs not found — install ESP32 core first))
	$$(if $(ESPTOOL),, $$(error esptool not found — install ESP32 core first))
	@echo "Building LittleFS image from $($(1)_SKETCH)/data …"
	$(MKLITTLEFS) -c $($(1)_SKETCH)/data \
		-p $(LITTLEFS_PAGE) -b $(LITTLEFS_BLOCK) \
		-s $(LITTLEFS_SIZE) \
		$(BUILD_DIR)/$(1)/littlefs.bin
	@echo "Flashing LittleFS image to $$(call detect_port) …"
	$(ESPTOOL) --chip esp32 --port $$(call detect_port) --baud 921600 \
		write_flash $(LITTLEFS_OFFSET) $(BUILD_DIR)/$(1)/littlefs.bin
endif

endef

# Expand targets for every variant
$(foreach v,$(VARIANTS),$(eval $(call VARIANT_TARGETS,$(v))))

# ──────────────────────────────────────────────
# Shorthand targets (use VARIANT variable)
# ──────────────────────────────────────────────
.PHONY: build upload flash monitor

build:   build-$(VARIANT)
upload:  upload-$(VARIANT)
flash:   flash-$(VARIANT)
monitor: monitor-$(VARIANT)

# ──────────────────────────────────────────────
# Host-side unit tests (no ESP32 needed)
# ──────────────────────────────────────────────
TEST_CXX      ?= clang++
TEST_CXXFLAGS := -std=c++17 -Wall -Wextra -g -O0
TEST_INCLUDES := -isystem test/mocks -I common -I test
TEST_SRCS     := test/test_main.cpp test/eventbus_impl.cpp \
                 test/test_detectors.cpp test/test_device_tracker.cpp \
                 test/test_threat_analyzer.cpp test/test_battery_smoothing.cpp
TEST_BIN      := $(BUILD_DIR)/test_runner
DOCTEST_URL   := https://raw.githubusercontent.com/doctest/doctest/v$(DOCTEST_VERSION)/doctest/doctest.h

.PHONY: test test-verbose fetch-doctest

fetch-doctest: test/doctest.h

test/doctest.h:
	@echo "Fetching doctest.h …"
	curl -sL -o $@ $(DOCTEST_URL)

test: fetch-doctest
	@mkdir -p $(BUILD_DIR)
	$(TEST_CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(TEST_SRCS) -o $(TEST_BIN)
	$(TEST_BIN)

test-verbose: fetch-doctest
	@mkdir -p $(BUILD_DIR)
	$(TEST_CXX) $(TEST_CXXFLAGS) $(TEST_INCLUDES) $(TEST_SRCS) -o $(TEST_BIN)
	$(TEST_BIN) --success

# ──────────────────────────────────────────────
# Global targets
# ──────────────────────────────────────────────
.PHONY: all clean install-deps help

all: $(foreach v,$(VARIANTS),build-$(v))

clean:
	rm -rf $(BUILD_DIR)

install-deps:
	arduino-cli core update-index
	arduino-cli core install esp32:esp32@$(CORE_VERSION)
	arduino-cli lib install \
		ArduinoJson@$(ARDUINOJSON_VERSION) \
		"NimBLE-Arduino@$(NIMBLE_VERSION)" \
		M5Unified@$(M5UNIFIED_VERSION) \
		U8g2@$(U8G2_VERSION) \
		"Adafruit GFX Library@$(ADAFRUIT_GFX_VERSION)" \
		"Adafruit SSD1306@$(ADAFRUIT_SSD1306_VERSION)"

# ──────────────────────────────────────────────
# Docker build environment
# ──────────────────────────────────────────────
DOCKER_IMAGE ?= flocksquawk-build:latest

.PHONY: docker-build-image docker-build-all docker-build docker-test \
        docker-test-verbose docker-shell docker-clean

docker-build-image:
	docker build -t $(DOCKER_IMAGE) \
		--build-arg BASE_IMAGE=$(BASE_IMAGE) \
		--build-arg ARDUINO_CLI_VERSION=$(ARDUINO_CLI_VERSION) \
		--build-arg ESP32_CORE_VERSION=$(CORE_VERSION) \
		--build-arg ARDUINOJSON_VERSION=$(ARDUINOJSON_VERSION) \
		--build-arg NIMBLE_VERSION=$(NIMBLE_VERSION) \
		--build-arg M5UNIFIED_VERSION=$(M5UNIFIED_VERSION) \
		--build-arg U8G2_VERSION=$(U8G2_VERSION) \
		--build-arg ADAFRUIT_GFX_VERSION=$(ADAFRUIT_GFX_VERSION) \
		--build-arg ADAFRUIT_SSD1306_VERSION=$(ADAFRUIT_SSD1306_VERSION) \
		--build-arg DOCTEST_VERSION=$(DOCTEST_VERSION) \
		.

docker-build-all:
	docker run --rm -v "$(CURDIR)":/workspace $(DOCKER_IMAGE) make all

docker-build:
	docker run --rm -v "$(CURDIR)":/workspace $(DOCKER_IMAGE) make build-$(VARIANT)

docker-test:
	docker run --rm -v "$(CURDIR)":/workspace $(DOCKER_IMAGE) make test

docker-test-verbose:
	docker run --rm -v "$(CURDIR)":/workspace $(DOCKER_IMAGE) make test-verbose

docker-shell:
	docker run --rm -it -v "$(CURDIR)":/workspace $(DOCKER_IMAGE) /bin/bash

docker-clean:
	docker rmi $(DOCKER_IMAGE)

# ──────────────────────────────────────────────
# Help (default target)
# ──────────────────────────────────────────────
.DEFAULT_GOAL := help

help:
	@echo "FlockSquawk Build System"
	@echo "========================"
	@echo ""
	@echo "Variants: $(VARIANTS)"
	@echo ""
	@echo "Global targets:"
	@echo "  make help              Show this message"
	@echo "  make all               Compile all variants"
	@echo "  make test              Run host-side unit tests"
	@echo "  make test-verbose      Run tests with per-assertion output"
	@echo "  make clean             Remove build output (.build/)"
	@echo "  make install-deps      Install ESP32 core and Arduino libraries"
	@echo ""
	@echo "Per-variant targets (replace <v> with a variant name above):"
	@echo "  make build-<v>         Compile sketch"
	@echo "  make upload-<v>        Upload firmware to device"
	@echo "  make flash-<v>         Compile + upload"
	@echo "  make monitor-<v>       Open serial monitor"
	@echo "  make upload-data-<v>   Flash LittleFS data (m5fire, mini12864, oled only)"
	@echo ""
	@echo "Shorthand (uses VARIANT, default: $(VARIANT)):"
	@echo "  make build             => build-\$$(VARIANT)"
	@echo "  make upload            => upload-\$$(VARIANT)"
	@echo "  make flash             => flash-\$$(VARIANT)"
	@echo "  make monitor           => monitor-\$$(VARIANT)"
	@echo ""
	@echo "Docker targets:"
	@echo "  make docker-build-image  Build the Docker image (all deps pre-baked)"
	@echo "  make docker-build-all    Compile all variants in container"
	@echo "  make docker-build        Compile VARIANT in container"
	@echo "  make docker-test         Run host-side unit tests in container"
	@echo "  make docker-test-verbose Run verbose tests in container"
	@echo "  make docker-shell        Interactive shell in container"
	@echo "  make docker-clean        Remove the Docker image"
	@echo ""
	@echo "Variables:"
	@echo "  PORT=<path>            Serial port  (auto-detected if unset)"
	@echo "  BAUD=<rate>            Monitor baud rate  (default: 115200)"
	@echo "  VARIANT=<name>         Default variant    (default: m5stick)"
	@echo "  CORE_VERSION=<ver>     ESP32 core version (default: 3.0.7)"
	@echo ""
	@echo "Examples:"
	@echo "  make build-m5stick"
	@echo "  make flash-oled PORT=/dev/cu.usbserial-0001"
	@echo "  make upload-data-mini12864 PORT=/dev/ttyUSB0"
	@echo "  make all"
