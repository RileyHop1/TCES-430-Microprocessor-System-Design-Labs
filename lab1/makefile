CC = arm-none-eabi-gcc

CFLAGS = -c \
	-mcpu=cortex-m7 \
	-mthumb \
	-mfpu=fpv5-sp-d16 \
	-mfloat-abi=hard \
	-std=gnu11 \
	-DSTM32F746xx \
	-I./Include

LDFLAGS = \
	-mcpu=cortex-m7 \
	-mthumb \
	-mfpu=fpv5-sp-d16 \
	-mfloat-abi=hard \
	-T STM32F746NGHX_FLASH.ld

final: lab.elf

main.o: Src/main.c
	$(CC) $(CFLAGS) $^ -o $@

startup.o: startup_stm32f746nghx.s
	$(CC) $(CFLAGS) $^ -o $@

lab.elf: main.o startup.o
	$(CC) $(LDFLAGS) $^ -o $@

load:
	openocd -f lab.cfg -c "program lab.elf verify reset exit"

clean:
	rm -f *.o *.elf *.map
