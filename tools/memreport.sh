#!/usr/bin/env bash
# memreport.sh - list top BSS symbols from ELF and map them to source files
ELF=build/NPR70_FreeRTOS.elf
MAP=build/NPR70_FreeRTOS.map
if [ ! -f "$ELF" ]; then
  echo "ELF not found: $ELF"
  exit 1
fi

echo "Top BSS symbols in $ELF (size, addr, symbol):"
arm-none-eabi-nm -S --size-sort "$ELF" | grep " B " | awk '{printf "%8s %s %s\n", $2, $1, $3}' | sort -rn | head -n 40

echo
if [ -f "$MAP" ]; then
  echo "Mapping to source files from $MAP (if available):"
  for sym in $(arm-none-eabi-nm -S --size-sort "$ELF" | grep " B " | awk '{print $3}' | sort | uniq | head -n 40); do
    grep -n "\b$sym\b" build/*.lst build/*.map 2>/dev/null | head -n 1 | sed -e "s/^/$sym: /"
  done
else
  echo "Map file $MAP not found; skipping source mapping."
fi
