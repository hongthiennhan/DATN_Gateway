#!/bin/bash

# Default values
BAUDRATE=${1:-115200}
DEVICE=${2:-/dev/ttyUSB0}

# Init UART
echo "Initializing UART with baudrate $BAUDRATE on $DEVICE..."
./main_app -B$BAUDRATE -d$DEVICE

# # Loop to send commands
# while true; do
#     # echo "Sending Direction1..."
#     # ./main_app --Direction1
#     # sleep 3
#     # echo "Sending Direction2..."
#     # ./main_app --Direction2
#     # sleep 3
#     # echo "Sending Direction3..."
#     # ./main_app --Direction3
#     # sleep 3 
#     # echo "Sending Relay_On..."
#     # ./main_app --Relay_On
#     # sleep 0.5
#     # echo "Sending Relay_Off..."
#     # ./main_app --Relay_Off
#     # sleep 0.5
#     echo "Sending Led_On..."
#     ./main_app --Led_On
#     sleep 0.5
#     echo "Sending Led_Off..."
#     ./main_app --Led_Off
#     sleep 0.5
#     echo "Sending Send_Status and reading response..."
#     ./main_app --Send_Status
#     # Delay 0.5 seconds before next iteration
#     sleep 0.5
# done