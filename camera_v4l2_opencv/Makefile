###############################################################################
#
# Copyright (c) 2016-2017, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#  * Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#  * Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#  * Neither the name of NVIDIA CORPORATION nor the names of its
#    contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
# OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
###############################################################################

# Clear the flags from env
CPPFLAGS :=
LDFLAGS :=

# Verbose flag
ifeq ($(VERBOSE), 1)
AT =
else
AT = @
endif

# ARM ABI of the target platform
ifeq ($(TEGRA_ARMABI),)
TEGRA_ARMABI ?= aarch64-linux-gnu
endif

# Location of the target rootfs
ifeq ($(shell uname -m), aarch64)
TARGET_ROOTFS :=
else
ifeq ($(TARGET_ROOTFS),)
$(error Please specify the target rootfs path if you are cross-compiling)
endif
endif

ifeq ($(shell uname -m), aarch64)
	CROSS_COMPILE :=
else
	CROSS_COMPILE ?= aarch64-unknown-linux-gnu-
endif
AS             = $(AT) $(CROSS_COMPILE)as
LD             = $(AT) $(CROSS_COMPILE)ld
CC             = $(AT) $(CROSS_COMPILE)gcc
CPP            = $(AT) $(CROSS_COMPILE)g++
AR             = $(AT) $(CROSS_COMPILE)ar
NM             = $(AT) $(CROSS_COMPILE)nm
STRIP          = $(AT) $(CROSS_COMPILE)strip
OBJCOPY        = $(AT) $(CROSS_COMPILE)objcopy
OBJDUMP        = $(AT) $(CROSS_COMPILE)objdump
NVCC           = $(AT) $(CUDA_PATH)/bin/nvcc -ccbin $(filter-out $(AT), $(CPP))

CPPFLAGS += -Wall -std=c++11 `pkg-config --cflags opencv4`
LDFLAGS += `pkg-config --libs opencv4`
APP := camera_v4l2_opencv

SRCS := \
	camera_v4l2_opencv.cpp \
	$(wildcard $(CLASS_DIR)/*.cpp)

OBJS := $(SRCS:.cpp=.o)

all: $(APP)

$(CLASS_DIR)/%.o: $(CLASS_DIR)/%.cpp
	$(AT)$(MAKE) -C $(CLASS_DIR)

%.o: %.cpp
	@echo "Compiling: $<"
	$(CPP) $(CPPFLAGS) -c $<

$(APP): $(OBJS)
	@echo "Linking: $@"
	$(CPP) -o $@ $(OBJS) $(CPPFLAGS) $(LDFLAGS)

clean:
	$(AT)rm -rf $(APP) $(OBJS)
