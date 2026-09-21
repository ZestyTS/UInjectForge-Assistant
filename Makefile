#-------------------------------------------------------------------------------
.SUFFIXES:
#-------------------------------------------------------------------------------

.DEFAULT_GOAL := all

HOSTCC ?= cc
HOSTCXX ?= c++
HOST_TEST_BUILD := build-host
HOST_TEST_ONLY := $(if $(filter host-test ui-host-test,$(MAKECMDGOALS)),$(if $(filter-out host-test ui-host-test clean,$(MAKECMDGOALS)),,yes),)

TOPDIR ?= $(CURDIR)

# wut_rules snapshots these values when it is included, so keep them above the
# include or the WUHB will silently fall back to blank identity metadata.
APP_NAME := UInjectForge Assistant
APP_SHORTNAME := UIF Assistant
APP_AUTHOR := ZestyTS
UIF_COMPONENT_VERSION ?= 1.0.0-preview.20260921
UIF_COMPONENT_BUILD_ID ?= dev

ifneq ($(HOST_TEST_ONLY),yes)
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

include $(DEVKITPRO)/wut/share/wut_rules
endif

TARGET := uinjectforge-assistant
BUILD := build
SOURCES := source
DATA :=
INCLUDES := include
CONTENT :=
ICON := assets/uinjectforge-icon.png
TV_SPLASH :=
DRC_SPLASH :=

CFLAGS := -g -Wall -O2 -ffunction-sections $(MACHDEP)
CFLAGS += $(INCLUDE) -D__WIIU__ -D__WUT__ \
	-DUIF_COMPONENT_VERSION=\"$(UIF_COMPONENT_VERSION)\" \
	-DUIF_COMPONENT_BUILD_ID=\"$(UIF_COMPONENT_BUILD_ID)\"
CXXFLAGS := $(CFLAGS) -std=gnu++17
ASFLAGS := -g $(ARCH)
LDFLAGS = -g $(ARCH) $(RPXSPECS) -Wl,-Map,$(notdir $*.map)
LIBS := -lSDL2 -lSDL2_ttf -lfreetype -lpng -lbz2 -lz -lmbedcrypto -lwut
LIBDIRS := $(PORTLIBS) $(WUT_ROOT) $(WUT_ROOT)/usr
.PHONY: host-test ui-host-test
ui-host-test:
	@mkdir -p $(HOST_TEST_BUILD)
	@$(HOSTCC) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -c source/assistant_policy.c -o $(HOST_TEST_BUILD)/assistant-policy.o
	@$(HOSTCC) -std=c11 -Wall -Wextra -Werror -DUIF_ASSISTANT_HOST_TEST -Iinclude -Itests -c source/assistant_storage.c -o $(HOST_TEST_BUILD)/assistant-storage.o
	@$(HOSTCXX) -std=c++17 -Wall -Wextra -Werror -DUIF_ASSISTANT_HOST_TEST -Iinclude -Itests $$(pkg-config --cflags sdl2 SDL2_ttf) tests/assistant_ui_tests.cpp $(HOST_TEST_BUILD)/assistant-storage.o $(HOST_TEST_BUILD)/assistant-policy.o -lmbedcrypto $$(pkg-config --libs sdl2 SDL2_ttf) -o $(HOST_TEST_BUILD)/assistant-ui-tests
	@SDL_VIDEODRIVER=dummy $(HOST_TEST_BUILD)/assistant-ui-tests $(HOST_TEST_BUILD)/screenshots

host-test:
	@mkdir -p $(HOST_TEST_BUILD)
	@sh tests/run-transfer-results.sh $(UIF_RESULT_FIXTURE)
	@$(HOSTCC) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude source/assistant_policy.c tests/assistant_policy_tests.c -lmbedcrypto -o $(HOST_TEST_BUILD)/assistant-policy-tests
	@$(HOST_TEST_BUILD)/assistant-policy-tests
	@$(HOSTCC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude source/assistant_json.c source/assistant_paths.c tests/assistant_json_tests.c -o $(HOST_TEST_BUILD)/assistant-json-tests
	@$(HOST_TEST_BUILD)/assistant-json-tests $(UIF_WIRE_FIXTURE)
	@$(HOSTCC) -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude source/assistant_install.c source/assistant_security.c tests/assistant_install_tests.c -o $(HOST_TEST_BUILD)/assistant-install-tests
	@$(HOST_TEST_BUILD)/assistant-install-tests
	@$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Iinclude source/assistant_security.c source/assistant_paths.c tests/assistant_security_tests.c -o $(HOST_TEST_BUILD)/assistant-security-tests
	@$(HOST_TEST_BUILD)/assistant-security-tests

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
    $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
    $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
    -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifneq (,$(strip $(CONTENT)))
export APP_CONTENT := $(TOPDIR)/$(CONTENT)
endif

ifneq (,$(strip $(ICON)))
export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifneq (,$(strip $(TV_SPLASH)))
export APP_TV_SPLASH := $(TOPDIR)/$(TV_SPLASH)
endif

ifneq (,$(strip $(DRC_SPLASH)))
export APP_DRC_SPLASH := $(TOPDIR)/$(DRC_SPLASH)
endif

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(HOST_TEST_BUILD) $(TARGET).wuhb $(TARGET).rpx $(TARGET).elf

else

.PHONY: all
DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).wuhb
# Rebuild the bundle when WUHB identity metadata changes in this Makefile.
$(OUTPUT).wuhb: $(TOPDIR)/Makefile
$(OUTPUT).wuhb: $(OUTPUT).rpx
$(OUTPUT).rpx: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)
$(OFILES_SRC): $(HFILES_BIN)

%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

endif
