#!/bin/bash
# Quick serial port test script for NPR-70

PORT=${1:-/dev/ttyUSB0}
BAUD=921600

echo "NPR-70 Serial Test"
echo "=================="
echo "Port: $PORT"
echo "Baud: $BAUD"
echo ""
echo "Instructions:"
echo "1. Connect your USB-UART adapter:"
echo "   - Adapter TX -> Board RX (PA3)"
echo "   - Adapter RX -> Board TX (PA2)"
echo "   - GND -> GND"
echo ""
echo "2. Reset the board (press RESET button)"
echo "3. You should see boot messages"
echo ""
echo "Starting screen in 3 seconds..."
echo "Press Ctrl+A then K to exit screen"
sleep 3

screen $PORT $BAUD
