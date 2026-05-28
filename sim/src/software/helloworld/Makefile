GCC = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy
OBJDUMP = riscv64-unknown-elf-objdump

CFLAGS_RV64 = -mabi=lp64 -march=rv64ima_zicsr -mcmodel=medany
LDFLAGS = -Tlinker.ld -nostdlib -static -nostartfiles

TARGET = hello

.PHONY: all clean

all: $(TARGET).bin $(TARGET).dump

$(TARGET).elf: hello_singlecore.S linker.ld
	$(GCC) $(CFLAGS_RV64) $(LDFLAGS) $< -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(TARGET).dump: $(TARGET).elf
	riscv64-unknown-elf-objdump -d -s $< > $@

clean:
	rm -f *.elf *.bin *.dump
