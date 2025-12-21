# Project Name
TARGET = hoopi_seed
CPP_STANDARD = -std=gnu++20

APP_TYPE = BOOT_SRAM

# https://stackoverflow.com/questions/1778538/how-many-gcc-optimization-levels-are-there
# -O (Same as -O1)
# -O0 (do no optimize, the default if no optimization level is specified)
# -O1 (optimize minimally, favoring compilation time)
# -O2 (optimize more, without speed/size tradeoff)
# -O3 (optimize even more, favoring speed)
# -Ofast (optimize very aggressively to the point of breaking standard compliance, favoring speed. May change program behavior)

OPT=-Ofast

# Uncomment to use LGPL (like ReverbSc, etc.)
USE_DAISYSP_LGPL=1
# USE_FATFS = 1

# Sources and Hothouse header files
CPP_SOURCES = hoopi.cpp hoopi_pedal.cpp effects/ImpulseResponse/ImpulseResponse.cpp effects/ImpulseResponse/dsp.cpp 
CPP_SOURCES += effects/base_effect_module.cpp
CPP_SOURCES += effects/amp_module.cpp
CPP_SOURCES += effects/chorus_module.cpp
CPP_SOURCES += effects/cloudseed_module.cpp
CPP_SOURCES += dependencies/CloudSeed/AudioLib/Biquad2.cpp
CPP_SOURCES += dependencies/CloudSeed/AudioLib/ShaRandom.cpp
CPP_SOURCES += dependencies/CloudSeed/AudioLib/ValueTables.cpp
CPP_SOURCES += dependencies/CloudSeed/FastSin.cpp
CPP_SOURCES += dependencies/CloudSeed/Utils/Sha256.cpp
CPP_SOURCES += effects/nam_module.cpp
CPP_SOURCES += effects/distortion_module.cpp
CPP_SOURCES += effects/looper_module.cpp
CPP_SOURCES += effects/galaxy_reverb_module.cpp
CPP_SOURCES += effects/delay_module.cpp
CPP_SOURCES += effects/tremolo_module.cpp

C_INCLUDES = -I.. -I ./include -I./dependencies/RTNeural -I./dependencies/RTNeural/modules/Eigen -I./dependencies/CloudSeed
C_INCLUDES += -isystem ./dependencies/q/q/q_lib/include
C_INCLUDES += -isystem ./dependencies/q/infra/include

# Library Locations
LIBDAISY_DIR = ./dependencies/libDaisy
DAISYSP_DIR = ./dependencies/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# Global helpers
# include ../Makefile

CPPFLAGS += -DRTNEURAL_DEFAULT_ALIGNMENT=8 -DRTNEURAL_NO_DEBUG=1 -DRTNEURAL_USE_EIGEN=1
# LDFLAGS += -u _printf_float

