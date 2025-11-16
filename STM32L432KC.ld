/**
 ******************************************************************************
 * @file      STM32L432KC.ld
 * @author    NPR-70 FreeRTOS Port
 * @brief     Linker script for STM32L432KC with FreeRTOS
 ******************************************************************************
 * @attention
 * 
 * Memory Layout:
 * - Flash: 256KB (0x08000000 - 0x0803FFFF)
 *   - Application: 254KB (0x08000000 - 0x0803F7FF)
 *   - Config Storage: 2KB (0x0803F800 - 0x0803FFFF) - Reserved for persistent config
 * 
 * - SRAM1: 64KB (0x20000000 - 0x2000FFFF)
 *   - Interrupt vectors in RAM (if needed): 0x188 bytes
 *   - Data, BSS, heap, stacks
 *   - FreeRTOS heap: 32KB (configured in FreeRTOSConfig.h)
 * 
 * - SRAM2: 16KB (0x10000000 - 0x10003FFF) - Can be used for DMA or special data
 ******************************************************************************
 */

/* Entry Point */
ENTRY(Reset_Handler)

/* Highest address of the user mode stack */
_estack = ORIGIN(SRAM1) + LENGTH(SRAM1);  /* End of SRAM1 */

/* FreeRTOS heap size - defined here for linker awareness */
_Min_Heap_Size = 0x3000;  /* 12KB for FreeRTOS heap_4 allocator (carefully tuned after SRAM2 optimization) */
_Min_Stack_Size = 0x600;  /* 1.5KB for main stack (MSP) (reduced from 2KB) */

/* Specify the memory areas */
MEMORY
{
  FLASH      (rx)  : ORIGIN = 0x08000000, LENGTH = 254K   /* Application flash */
  CONFIG     (r)   : ORIGIN = 0x0803F800, LENGTH = 2K     /* Persistent config storage */
  SRAM1      (rwx) : ORIGIN = 0x20000000, LENGTH = 64K    /* Main SRAM */
  SRAM2      (rwx) : ORIGIN = 0x10000000, LENGTH = 16K    /* SRAM2 (optional for DMA) */
}

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into FLASH */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))  /* Startup code */
    . = ALIGN(4);
  } >FLASH

  /* The program code and other data goes into FLASH */
  .text :
  {
    . = ALIGN(4);
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(4);
    _etext = .;        /* define a global symbols at end of code */
  } >FLASH

  /* Constant data goes into FLASH */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array     :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH
  
  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH
  
  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH

  /* used by the startup to initialize data */
  _sidata = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  .data : 
  {
    . = ALIGN(4);
    _sdata = .;        /* create a global symbol at data start */
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */

    . = ALIGN(4);
    _edata = .;        /* define a global symbol at data end */
  } >SRAM1 AT> FLASH

  /* Uninitialized data section */
  . = ALIGN(4);
  .bss :
  {
    /* This is used by the startup in order to initialize the .bss section */
    _sbss = .;         /* define a global symbol at bss start */
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;         /* define a global symbol at bss end */
    __bss_end__ = _ebss;
  } >SRAM1

  /* User_heap_stack section, used to check that there is enough RAM left */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >SRAM1

  /* FreeRTOS heap area - this is where pvPortMalloc() allocates from */
  .freertos_heap (NOLOAD):
  {
    . = ALIGN(8);
    __freertos_heap_start = .;
    . = . + _Min_Heap_Size;
    __freertos_heap_end = .;
    . = ALIGN(8);
  } >SRAM1

  /* Config storage section (read-only at runtime, written by flash driver) */
  .config_storage :
  {
    . = ALIGN(4);
    KEEP(*(.config_storage))
    . = ALIGN(4);
  } >CONFIG

  /* SRAM2 section - can be used for DMA buffers or critical data */
  .sram2 (NOLOAD) :
  {
    . = ALIGN(4);
    __sram2_start = .;
    *(.sram2)
    *(.sram2*)
    . = ALIGN(4);
    __sram2_end = .;
  } >SRAM2

  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
