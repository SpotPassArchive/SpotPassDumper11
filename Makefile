.SUFFIXES:

SHELL := /usr/bin/env bash
DEVKITPRO	:=	/opt/devkitpro

PATH	:=	$(DEVKITPRO)/tools/bin:$(DEVKITPRO)/devkitARM/bin:$(DEVKITPRO)/portlibs/3ds/bin:$(PATH)

CC	:=	arm-none-eabi-gcc
LD	:=	$(CC)
CXX	:=	arm-none-eabi-g++
AS	:=	arm-none-eabi-as
AR	:=	arm-none-eabi-gcc-ar
OBJCOPY	:=	arm-none-eabi-objcopy
STRIP	:=	arm-none-eabi-strip
NM	:=	arm-none-eabi-gcc-nm
RANLIB	:=	arm-none-eabi-gcc-ranlib

TARGET		:=	SpotPassDumper11
BUILD		:=	build
SOURCES		:=	source
DATA		:=	data

ICON		:=	icon.png
APP_TITLE	:=	SpotPassDumper11
APP_DESCRIPTION :=	Extracts SpotPass cache data.
APP_AUTHOR	:= ZeroSkill

APP_VER_MAJ	:= 1
APP_VER_MIN	:= 0
APP_VER_MIC	:= 0

ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS	:=	-g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH)

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(BUILD)/$(TARGET).map -z execstack

LIBS	:=  -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lctru
LIBPATHS	:= $(DEVKITPRO)/libctru/lib $(DEVKITPRO)/portlibs/3ds/lib
LIBFLAGS	:= $(foreach lib,$(LIBPATHS),-L$(lib))

ROOT_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
OUTPUT	:=	$(ROOT_DIR)/$(TARGET)

VPATH	:=	$(foreach dir,$(SOURCES),$(ROOT_DIR)/$(dir)) \
					$(foreach dir,$(DATA),$(ROOT_DIR)/$(dir))

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

OFILES_SOURCES 	:=	$(CFILES:.c=.o)
OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
OFILES	:= $(foreach ofile,$(OFILES_BIN) $(OFILES_SOURCES),$(BUILD)/$(ofile))
HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))
INCLUDE	:=	-Iinclude \
			-I$(ROOT_DIR)/$(BUILD) \
			-I$(DEVKITPRO)/libctru/include \
			-I$(DEVKITPRO)/portlibs/3ds/include

CFLAGS	+=	$(INCLUDE) -D__3DS__

.PHONY: all clean cia

all: $(OUTPUT).3dsx
cia: $(OUTPUT).cia

clean:
	rm -fr $(BUILD) $(TARGET).cia $(TARGET).3dsx $(TARGET).elf

$(BUILD):
	@mkdir -p $@

$(BUILD)/%.o: %.c
	@echo [c] $(notdir $<)
	@$(CC) -MMD -MP -MF $(BUILD)/$*.d $(CPPFLAGS) $(CFLAGS) -c $< -o $@ $(ERROR_FILTER)

$(BUILD)/%.bin.o	:	$(DATA)/%.bin
	@echo [bin-embed] $(notdir $<)
	@bin2s -a 4 -H $(@:.bin.o=_bin.h) $< | awk 1 | arm-none-eabi-as -o $@

%.3dsx: %.elf
	@echo [3dsx] $(notdir $@)
	@3dsxtool --smdh=$(ROOT_DIR)/cia-rsrc/icon.smdh  $< $@ 

%.elf:
	@echo [linking] $(notdir $@)
	@$(LD) $(LDFLAGS) $(OFILES) $(LIBFLAGS) $(LIBS) -o $@
	@$(NM) -CSn $@ > $(BUILD)/$(TARGET).lst

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	: $(BUILD)	$(OFILES)

$(OUTPUT).3dsx	:	$(OUTPUT).elf

$(OUTPUT).cia:	$(OUTPUT).elf 
	@echo [cia] $(notdir $@) \(v$(APP_VER_MAJ).$(APP_VER_MIN).$(APP_VER_MIC)\)
	@makerom -f cia -o $(OUTPUT).cia -target t -elf $(OUTPUT).elf -icon $(ROOT_DIR)/cia-rsrc/icon.smdh -banner $(ROOT_DIR)/cia-rsrc/banner.bnr -rsf $(ROOT_DIR)/cia-rsrc/spd11.rsf -ignoresign -v -major $(APP_VER_MAJ) -minor $(APP_VER_MIN) -micro $(APP_VER_MIC)

-include $(BUILD)/*.d
