TARGET = stage2
OBJS = start.o stage2.o

CC = gcc
OBJCOPY = objcopy
CFLAGS = -DSMP -isystem ../freebsd-headers/include -Wl,--build-id=none -Os -fno-stack-protector 
LDFLAGS = -T linker.ld -nostartfiles -nostdlib

ifdef ENABLE_DEBUG_MENU_OFF
CFLAGS += -UENABLE_DEBUG_MENU=1
endif

ifdef MODULE_DUMPER_OFF
CFLAGS += -UMODULE_DUMPER
endif

ifdef ENABLE_DEBUG_MENU
CFLAGS += -DENABLE_DEBUG_MENU=1
CFLAGS += -UMODULE_DUMPER
CFLAGS += -UUSB_LOADER
endif

ifdef MODULE_DUMPER
CFLAGS += -DUSB_LOADER=1
CFLAGS += -DMODULE_DUMPER=1
CFLAGS += -UENABLE_DEBUG_MENU
endif

ifdef MIRA
CFLAGS += -DMIRA=1
CFLAGS += -UMODULE_DUMPER
CFLAGS += -UENABLE_DEBUG_MENU
CFLAGS += -UUSB_LOADER
endif

ifdef USB_LOADER
CFLAGS += -DUSB_LOADER=1
CFLAGS += -UMODULE_DUMPER
CFLAGS += -UENABLE_DEBUG_MENU
endif

ifneq ($(filter $(FW), 850 900 903 904 950 960 1000 1001 1050 1070 1071 1100),)
CFLAGS += -DFIRMWARE=$(FW)
else
$(error "Invalid firmware")
endif

all: $(TARGET).bin

%.bin: %.elf
	$(OBJCOPY) -S -O binary $^ $@

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Adjusted to ensure correct handling of assembly files
%.o: %.asm
	$(CC) -x assembler-with-cpp -c $< -o $@

clean:
	@rm -f $(TARGET).bin $(TARGET).elf $(OBJS)
