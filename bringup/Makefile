TARGET  = bringup
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy

CFLAGS  = -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
CFLAGS += -O0 -g -Wall
CFLAGS += -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables
CFLAGS += -nostdlib

LDFLAGS = -T stm32f723.ld -nostdlib -Wl,--gc-sections

SRCS = main.c startup.c usb_host.c usb_device.c sdcard.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET).elf $(TARGET).bin

$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

flash: $(TARGET).elf
	probe-rs download --chip STM32F723VETx --probe 1fc9:0090 $(TARGET).elf
	probe-rs reset --chip STM32F723VETx --probe 1fc9:0090

# ---- Connectivity test (standalone, own main) ----
TEST_TARGET = conn_test
TEST_SRCS   = conn_test.c startup.c
TEST_OBJS   = $(TEST_SRCS:.c=.o)

$(TEST_TARGET).elf: conn_test.o startup.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(TEST_TARGET).bin: $(TEST_TARGET).elf
	$(OBJCOPY) -O binary $< $@

flash_test: $(TEST_TARGET).elf $(TEST_TARGET).bin
	probe-rs download --chip STM32F723VETx --probe 1fc9:0090 $(TEST_TARGET).elf
	probe-rs reset --chip STM32F723VETx --probe 1fc9:0090

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin \
	      conn_test.o $(TEST_TARGET).elf $(TEST_TARGET).bin
