# Host build of the real firmware on the virtual board.
# Usage: make -C sim <scenario>     (builds sim/build/<scenario>)
#        make -C sim run-<scenario>
#        make -C sim all
#
# NOT an idf.py build — this compiles the firmware sources natively
# against sim/esp_shim (ESP-IDF/FreeRTOS API) + sim/vboard (chip models).

CC      ?= gcc
TOP     := $(abspath ..)
SIM     := $(abspath .)
BUILD   := $(SIM)/build
IDF     := $(HOME)/esp/esp-idf

FW_DIRS  := $(wildcard $(TOP)/components/*)
FW_SRCS  := $(foreach d,$(FW_DIRS),$(wildcard $(d)/*.c $(d)/src/*.c))
# main.c is compiled with app_main renamed so scenarios own main().
FW_SRCS  += $(TOP)/main/main.c

SHIM_SRCS   := $(foreach d,$(wildcard $(SIM)/esp_shim/*),$(wildcard $(d)/*.c))
VBOARD_SRCS := $(foreach d,$(wildcard $(SIM)/vboard/*),$(wildcard $(d)/*.c)) \
               $(wildcard $(SIM)/vboard/*.c)
VSIM_SRCS   := $(wildcard $(SIM)/vsim/*.c)
# Real cJSON from IDF (same code the cloud client links on target).
CJSON_SRC   := $(IDF)/components/json/cJSON/cJSON.c

# Include-path discipline vs the features.h landmine
# (components/vq/include/features.h vs glibc <features.h>):
#  * firmware dirs on -iquote  -> firmware's quoted  "features.h" resolves ITS copy
#  * firmware dirs on -idirafter (searched AFTER system) -> bracketed <vq_*.h>
#    still resolves; libc <features.h> keeps winning for libc/angle includes.
SIM_INCS := $(addprefix -I,$(SIM)/esp_shim/include $(SIM)/vboard $(SIM)/vsim)
FW_DIRS_INC := $(sort $(dir $(wildcard $(TOP)/components/*/include))) \
               $(sort $(dir $(wildcard $(TOP)/components/*/include/*))) \
               $(TOP)/components $(TOP)/main $(IDF)/components/json/cJSON \
               $(foreach d,$(FW_DIRS),$(if $(wildcard $(d)/include),$(d)/include)) \
               $(foreach d,$(FW_DIRS),$(d))
FW_INCS  := $(addprefix -iquote ,$(FW_DIRS_INC)) $(addprefix -idirafter ,$(FW_DIRS_INC))

CFLAGS ?= -O2 -g -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
          -DVSIM=1 -Dapp_main=fw_app_main -DSIM_HOST_BUILD \
          $(SIM_INCS) $(FW_INCS) -pthread
LDFLAGS += -pthread -lm

# Private headers some components share via sibling dirs (st_priv.h etc.)
CFLAGS += $(foreach d,$(FW_DIRS),-I$(d) -I$(d)/src)

OBJS = $(FW_SRCS:%.c=$(BUILD)/%.o) $(SHIM_SRCS:%.c=$(BUILD)/%.o) \
       $(VBOARD_SRCS:%.c=$(BUILD)/%.o) $(VSIM_SRCS:%.c=$(BUILD)/%.o) \
       $(BUILD)/$(CJSON_SRC:.c=.o)

SCENARIOS := $(basename $(notdir $(wildcard $(SIM)/tests/*.c)))

.SECONDARY:

# OBJS embed absolute source paths (build//home/.../x.o) — the stem is
# 'home/...', so the prereq must be absolute (/%), else it resolves to
# a nonexistent relative path and make treats stale objects as fresh.
$(BUILD)/%.o: /%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%: $(SIM)/tests/%.c $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY: all run-% list $(SCENARIOS)
all: $(addprefix $(BUILD)/,$(SCENARIOS))

# documented usage: `make -C sim <scenario>` -> builds build/<scenario>
$(SCENARIOS): %: $(BUILD)/%

run-%: $(BUILD)/%
	$<

list:
	@echo "$(SCENARIOS)"
