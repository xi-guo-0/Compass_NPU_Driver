# Copyright (C) 2023-2024 Arm Technology (China) Co. Ltd.
#
# SPDX-License-Identifier: Apache-2.0

MD  := mkdir -p
RM  := rm -rf

SRC_ROOT := ./src
UMD_INC := ../driver/umd/include
TARGET := $(BUILD_AIPU_DRV_ODIR)/aipu_$(BUILD_TEST_CASE)
CXXFLAGS := -Wall -Werror -std=c++14 -I$(SRC_ROOT) -I$(UMD_INC)
LDFLAGS = -L$(BUILD_AIPU_DRV_ODIR) -laipudrv
ANDROIDCXXFLAGS := --target=aarch64-none-linux-android21 \
                   -I$(BUILD_ANDROID_NDK)/sysroot/usr/include \
                   --gcc-toolchain=$(BUILD_ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64 \
                   --sysroot=$(BUILD_ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
                   -DGTEST_HAS_RTTI=0 -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -DANDROID -Wno-error \
                   -Wno-non-pod-varargs -Wno-missing-braces -Wno-inconsistent-missing-override -Wno-unused-variable -Wno-overloaded-virtual \
                   -Wno-tautological-pointer-compare -Wno-unused-command-line-argument
ANDROIDLDFLAGS := -Wl,-O3 -Wl,--gc-sections \
                  -Wl,--exclude-libs,$(BUILD_ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/lib/gcc/aarch64-linux-android/4.9.x/libgcc.a \
                  -Wl,--exclude-libs,$(BUILD_ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/aarch64-linux-android/lib64/libatomic.a \
                  -static-libstdc++ -Wl,--build-id -Wl,--warn-shared-textrel -Wl,--fatal-warnings -Qunused-arguments -Wl,-z,noexecstack  -Wl,-z,defs

ifeq ($(BUILD_TARGET_PLATFORM), sim)
    ifeq ($(BUILD_AIPU_VERSION), aipu_v3)
        LDFLAGS += -L$(CONFIG_DRV_BRENVAR_X2_SIM_LPATH) -l$(COMPASS_DRV_BRENVAR_X2_SIM_LNAME)
    endif

    ifeq ($(BUILD_AIPU_VERSION), aipu_v3_1)
        LDFLAGS += -L$(CONFIG_DRV_BRENVAR_X3_SIM_LPATH) -l$(COMPASS_DRV_BRENVAR_X3_SIM_LNAME)
    endif
endif

ifeq ($(BUILD_TARGET_PLATFORM), sim)
    CXXFLAGS += -DSIMULATION=1
endif

ifeq ($(BUILD_DEBUG_FLAG), debug)
    CXXFLAGS += -O0 -g -DRTDEBUG=1
else
    CXXFLAGS += -O2 -DRTDEBUG=0
endif

ifeq ($(BUILD_AIPU_VERSION), aipu_v1v2)
    CXXFLAGS += -DZHOUYI_V12
endif

ifeq ($(BUILD_AIPU_VERSION), aipu_v3)
    CXXFLAGS += -DZHOUYI_V12
    CXXFLAGS += -DZHOUYI_V3
endif

ifeq ($(BUILD_AIPU_VERSION), aipu_v3_1)
    CXXFLAGS += -DZHOUYI_V12
    CXXFLAGS += -DZHOUYI_V3_1
endif

ifneq ($(BUILD_ANDROID_NDK), )
    LDFLAGS += -pthread -llog -ldl
    CXXFLAGS += $(ANDROIDCXXFLAGS)
    LDFLAGS += $(ANDROIDCXXFLAGS) $(ANDROIDLDFLAGS)
else
    LDFLAGS += -lpthread
endif

SRCS += $(wildcard $(SRC_ROOT)/common/cmd_line_parsing.cpp) \
        $(wildcard $(SRC_ROOT)/common/helper.cpp)

ifeq ($(BUILD_TEST_CASE), dmabuf_producer_consumer_test)
SRC_DIRS := $(SRC_ROOT)/common $(SRC_ROOT)/$(BUILD_TEST_CASE)
SRC_DIRS := $(patsubst $(SRC_ROOT)/%, %, $(SRC_DIRS))

SRCS += $(SRC_ROOT)/$(BUILD_TEST_CASE)/common.cpp
SRCS_MAIN = $(SRCS)
SRCS_MAIN += $(SRC_ROOT)/$(BUILD_TEST_CASE)/dmabuf_producer_consumer.cpp
OBJS_MAIN = $(patsubst $(SRC_ROOT)/%.cpp, $(COMPASS_DRV_BTENVAR_TEST_BUILD_DIR)/%.o, $(SRCS_MAIN))
TARGET_MAIN = $(BUILD_AIPU_DRV_ODIR)/aipu_dmabuf_producer_consumer_test

SRCS_PRODUCER = $(SRCS)
SRCS_PRODUCER += $(SRC_ROOT)/$(BUILD_TEST_CASE)/producer.cpp
OBJS_PRODUCER = $(patsubst $(SRC_ROOT)/%.cpp, $(COMPASS_DRV_BTENVAR_TEST_BUILD_DIR)/%.o, $(SRCS_PRODUCER))
TARGET_PRODUCER = $(BUILD_AIPU_DRV_ODIR)/aipu_dmabuf_producer

SRCS_CONSUMER = $(SRCS)
SRCS_CONSUMER += $(SRC_ROOT)/$(BUILD_TEST_CASE)/consumer.cpp
OBJS_CONSUMER = $(patsubst $(SRC_ROOT)/%.cpp, $(COMPASS_DRV_BTENVAR_TEST_BUILD_DIR)/%.o, $(SRCS_CONSUMER))
TARGET_CONSUMER = $(BUILD_AIPU_DRV_ODIR)/aipu_dmabuf_consumer

all: build-repo $(TARGET_PRODUCER) $(TARGET_CONSUMER) $(TARGET_MAIN)
else
SRCS += $(wildcard $(SRC_ROOT)/$(BUILD_TEST_CASE)/*.cpp)
SRC_DIRS := $(SRC_ROOT)/common $(SRC_ROOT)/$(BUILD_TEST_CASE)
SRC_DIRS := $(patsubst $(SRC_ROOT)/%, %, $(SRC_DIRS))
OBJS = $(patsubst $(SRC_ROOT)/%.cpp, $(COMPASS_DRV_BTENVAR_TEST_BUILD_DIR)/%.o, $(SRCS))

all: build-repo $(TARGET)
endif

$(COMPASS_DRV_BTENVAR_TEST_BUILD_DIR)/%.o: $(SRC_ROOT)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

ifeq ($(BUILD_TEST_CASE), dmabuf_producer_consumer_test)
$(TARGET_PRODUCER): $(OBJS_PRODUCER)
	$(CXX) $^ $(LDFLAGS) -o $@

$(TARGET_CONSUMER): $(OBJS_CONSUMER)
	$(CXX) $^ $(LDFLAGS) -o $@

$(TARGET_MAIN): $(OBJS_MAIN)
	$(CXX) $^ $(LDFLAGS) -o $@
else
$(TARGET): $(OBJS)
	$(CXX) $^ $(LDFLAGS) -o $@
	$(RM) $(OBJS)
endif

build-repo:
	@$(call make-repo)

clean:
	$(RM) $(OBJS) $(BUILD_AIPU_DRV_ODIR)/*test

.PHONY: all clean

define make-repo
	$(MD) $(COMPASS_DRV_BTENVAR_TEST_BUILD_DIR)
	for dir in $(SRC_DIRS); \
	do \
		$(MD) $(COMPASS_DRV_BTENVAR_TEST_BUILD_DIR)/$$dir; \
	done
endef
