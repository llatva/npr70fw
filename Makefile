######################################
# NPR-70 FreeRTOS Makefile
######################################

TARGET = NPR70_FreeRTOS
DEBUG = 1
OPT = -Og
BUILD_DIR = build

C_SOURCES = Core/Src/main.c
C_SOURCES += Core/Src/system_stm32l4xx.c
C_SOURCES += Core/Src/syscalls.c
C_SOURCES += Core/Src/stm32l4xx_hal_msp.c
C_SOURCES += Core/Src/stm32l4xx_it.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_cortex.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rcc.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rcc_ex.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash_ex.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash_ramfunc.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_gpio.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dma.c
# C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dma_ex.c  # Not in mbed HAL version
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pwr.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pwr_ex.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_tim.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_tim_ex.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_uart.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_uart_ex.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_spi.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_spi_ex.c
C_SOURCES += Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_iwdg.c
C_SOURCES += Middleware/FreeRTOS/tasks.c
C_SOURCES += Middleware/FreeRTOS/queue.c
C_SOURCES += Middleware/FreeRTOS/list.c
C_SOURCES += Middleware/FreeRTOS/timers.c
C_SOURCES += Middleware/FreeRTOS/event_groups.c
C_SOURCES += Middleware/FreeRTOS/stream_buffer.c
C_SOURCES += Middleware/FreeRTOS/portable/GCC/ARM_CM4F/port.c
C_SOURCES += Middleware/FreeRTOS/portable/MemMang/heap_4.c

# Application sources
C_SOURCES += Application/Ethernet/w5500_driver.c
C_SOURCES += Application/Radio/si4463_driver.c
C_SOURCES += Application/Memory/ext_sram_driver.c
C_SOURCES += Application/Memory/config_flash.c
C_SOURCES += Application/Common/app_common.c
C_SOURCES += Application/Common/fec_codec.c
C_SOURCES += Application/Common/watchdog.c

# Task sources (combined implementations only)
C_SOURCES += Application/Tasks/task_radio_combined.c
C_SOURCES += Application/Tasks/task_tdma.c
C_SOURCES += Application/Tasks/task_signaling.c
C_SOURCES += Application/Tasks/task_dhcp_arp.c
C_SOURCES += Application/Tasks/task_snmp.c
C_SOURCES += Application/Tasks/task_telnet.c
C_SOURCES += Application/Tasks/task_serial_cli.c
C_SOURCES += Application/Tasks/task_ethernet.c
C_SOURCES += Application/Tasks/task_monitor.c

# Common/shared modules
C_SOURCES += Application/Common/cli_commands.c

ASM_SOURCES = Core/Src/startup_stm32l432xx.s

PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size
HEX = $(CP) -O ihex
BIN = $(CP) -O binary -S

CPU = -mcpu=cortex-m4
FPU = -mfpu=fpv4-sp-d16
FLOAT-ABI = -mfloat-abi=hard
MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

C_DEFS = -DUSE_HAL_DRIVER -DSTM32L432xx -DARM_MATH_CM4 -D__FPU_PRESENT=1

C_INCLUDES = -ICore/Inc
C_INCLUDES += -IApplication/Common
C_INCLUDES += -IApplication/Tasks
C_INCLUDES += -IApplication/Ethernet
C_INCLUDES += -IApplication/Radio
C_INCLUDES += -IApplication/Memory
C_INCLUDES += -IDrivers/STM32L4xx_HAL_Driver/Inc
C_INCLUDES += -IDrivers/STM32L4xx_HAL_Driver/Inc/Legacy
C_INCLUDES += -IDrivers/CMSIS/Device/ST/STM32L4xx/Include
C_INCLUDES += -IDrivers/CMSIS/Include
C_INCLUDES += -IMiddleware/FreeRTOS/include
C_INCLUDES += -IMiddleware/FreeRTOS/portable/GCC/ARM_CM4F

ASFLAGS = $(MCU) $(OPT) -Wall -fdata-sections -ffunction-sections
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif

CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

LDSCRIPT = STM32L432KC.ld
LIBS = -lc -lm -lnosys
LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) $(LIBS) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir $@

clean:
	-rm -fR $(BUILD_DIR)

flash: all
	st-flash write $(BUILD_DIR)/$(TARGET).bin 0x08000000

-include $(wildcard $(BUILD_DIR)/*.d)

