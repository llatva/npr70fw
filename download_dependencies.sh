#!/bin/bash
###############################################################################
# NPR-70 FreeRTOS Port - Dependency Download Script
# Downloads FreeRTOS kernel and STM32 HAL drivers
###############################################################################

set -e  # Exit on error

echo "======================================================================"
echo "NPR-70 FreeRTOS Port - Downloading Dependencies"
echo "======================================================================"
echo ""

# Color codes for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

###############################################################################
# Download FreeRTOS Kernel LTS 11.1.0
###############################################################################

echo -e "${YELLOW}[1/2] Downloading FreeRTOS Kernel LTS 11.1.0...${NC}"

if [ -d "Middleware/FreeRTOS" ]; then
    echo -e "${YELLOW}FreeRTOS already exists. Skipping download.${NC}"
else
    mkdir -p Middleware
    cd Middleware
    
    echo "Cloning FreeRTOS kernel repository..."
    git clone --branch V11.1.0 --depth 1 \
        https://github.com/FreeRTOS/FreeRTOS-Kernel.git FreeRTOS
    
    cd FreeRTOS
    
    # Remove unnecessary files to save space
    echo "Cleaning up unnecessary files..."
    rm -rf .github
    rm -rf Demo
    rm -rf docs
    rm -f .gitignore
    
    cd ../..
    
    echo -e "${GREEN}✓ FreeRTOS kernel downloaded successfully${NC}"
fi

echo ""

###############################################################################
# Download STM32CubeL4 HAL Drivers
###############################################################################

echo -e "${YELLOW}[2/2] Downloading STM32CubeL4 HAL Drivers...${NC}"

if [ -d "Drivers/STM32L4xx_HAL_Driver" ] && [ -d "Drivers/CMSIS" ]; then
    echo -e "${YELLOW}STM32 HAL already exists. Skipping download.${NC}"
else
    mkdir -p Drivers
    cd Drivers
    
    echo "Cloning STM32CubeL4 repository (this may take a few minutes)..."
    git clone --depth 1 \
        https://github.com/STMicroelectronics/STM32CubeL4.git temp_stm32cube
    
    echo "Extracting HAL drivers..."
    cp -r temp_stm32cube/Drivers/STM32L4xx_HAL_Driver .
    
    echo "Extracting CMSIS files..."
    cp -r temp_stm32cube/Drivers/CMSIS .
    
    echo "Cleaning up temporary files..."
    rm -rf temp_stm32cube
    
    cd ..
    
    echo -e "${GREEN}✓ STM32 HAL drivers downloaded successfully${NC}"
fi

echo ""

###############################################################################
# Verify downloaded files
###############################################################################

echo -e "${YELLOW}Verifying downloaded files...${NC}"
echo ""

ERRORS=0

# Check FreeRTOS
if [ -f "Middleware/FreeRTOS/Source/tasks.c" ]; then
    echo -e "${GREEN}✓${NC} FreeRTOS kernel: tasks.c found"
else
    echo -e "${RED}✗${NC} FreeRTOS kernel: tasks.c NOT FOUND"
    ERRORS=$((ERRORS + 1))
fi

if [ -f "Middleware/FreeRTOS/Source/queue.c" ]; then
    echo -e "${GREEN}✓${NC} FreeRTOS kernel: queue.c found"
else
    echo -e "${RED}✗${NC} FreeRTOS kernel: queue.c NOT FOUND"
    ERRORS=$((ERRORS + 1))
fi

if [ -d "Middleware/FreeRTOS/Source/portable/GCC/ARM_CM4F" ]; then
    echo -e "${GREEN}✓${NC} FreeRTOS port: ARM_CM4F found"
else
    echo -e "${RED}✗${NC} FreeRTOS port: ARM_CM4F NOT FOUND"
    ERRORS=$((ERRORS + 1))
fi

if [ -f "Middleware/FreeRTOS/Source/portable/MemMang/heap_4.c" ]; then
    echo -e "${GREEN}✓${NC} FreeRTOS heap: heap_4.c found"
else
    echo -e "${RED}✗${NC} FreeRTOS heap: heap_4.c NOT FOUND"
    ERRORS=$((ERRORS + 1))
fi

# Check STM32 HAL
if [ -f "Drivers/STM32L4xx_HAL_Driver/Inc/stm32l4xx_hal.h" ]; then
    echo -e "${GREEN}✓${NC} STM32 HAL: Header file found"
else
    echo -e "${RED}✗${NC} STM32 HAL: Header file NOT FOUND"
    ERRORS=$((ERRORS + 1))
fi

if [ -f "Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal.c" ]; then
    echo -e "${GREEN}✓${NC} STM32 HAL: Source file found"
else
    echo -e "${RED}✗${NC} STM32 HAL: Source file NOT FOUND"
    ERRORS=$((ERRORS + 1))
fi

if [ -f "Drivers/CMSIS/Include/core_cm4.h" ]; then
    echo -e "${GREEN}✓${NC} CMSIS: Cortex-M4 core headers found"
else
    echo -e "${RED}✗${NC} CMSIS: Cortex-M4 core headers NOT FOUND"
    ERRORS=$((ERRORS + 1))
fi

if [ -f "Drivers/CMSIS/Device/ST/STM32L4xx/Include/stm32l432xx.h" ]; then
    echo -e "${GREEN}✓${NC} CMSIS: STM32L432 device headers found"
else
    echo -e "${RED}✗${NC} CMSIS: STM32L432 device headers NOT FOUND"
    ERRORS=$((ERRORS + 1))
fi

echo ""

###############################################################################
# Summary
###############################################################################

if [ $ERRORS -eq 0 ]; then
    echo -e "${GREEN}======================================================================"
    echo -e "SUCCESS: All dependencies downloaded and verified!"
    echo -e "======================================================================${NC}"
    echo ""
    echo "Directory structure:"
    echo "  Middleware/FreeRTOS/       - FreeRTOS kernel"
    echo "  Drivers/STM32L4xx_HAL_Driver/ - STM32 HAL drivers"
    echo "  Drivers/CMSIS/             - CMSIS headers"
    echo ""
    echo "Next steps:"
    echo "  1. Review Core/Inc/FreeRTOSConfig.h"
    echo "  2. Review Core/Inc/main.h"
    echo "  3. Create linker script (STM32L432KC.ld)"
    echo "  4. Implement Core/Src/main.c"
    echo "  5. Create Makefile or CMakeLists.txt"
    echo ""
    echo "See IMPLEMENTATION_PLAN.md for detailed next steps."
else
    echo -e "${RED}======================================================================"
    echo -e "ERROR: Download verification failed with $ERRORS errors"
    echo -e "======================================================================${NC}"
    echo ""
    echo "Please check your internet connection and try again."
    echo "If the problem persists, you may need to download manually from:"
    echo "  - FreeRTOS: https://github.com/FreeRTOS/FreeRTOS-Kernel"
    echo "  - STM32CubeL4: https://github.com/STMicroelectronics/STM32CubeL4"
    exit 1
fi
