# Bug: Spurious Recording Start

## Symptom

Occasionally, the ESP32 would unexpectedly start recording without user input. The log showed:

```
I (8915) main_task: Returned from app_main()
I (76095) HOOPI: Set param cmd sent: effect=1, param=20, value=5, extra=0
I (76755) HOOPI: Effect 1 param 20 persisted to NVS: 5
I (133215) HOOPI: Daisy Seed toggle values received: 1, 1, 1
I (133215) HOOPI: Daisy ready (toggle values received), scheduling initial sync...
I (134235) HOOPI: Daisy Seed active effect: 1
I (134515) HOOPI: Start record command received.
I (134525) HOOPI: File opened: /sdcard/93_hoopi_D2A4.wav
I (134535) HOOPI: Recording started ....
```

## Root Cause

The UART receive buffer (`uart_recv_buffer`) was not being cleared between reads in `rx_task()`. This caused two issues:

1. **Stale data interpretation**: Old bytes from previous packets could remain in the buffer and be misinterpreted as new commands.

2. **Byte misalignment**: The pattern of `1`s in the toggle values `(6, 1, 1, 1)` and effect switch `(7, 1)` messages, combined with potential byte loss or timing issues, could result in data bytes being interpreted as command bytes.

For example, if byte `7` (effect switch command) was lost or delayed, the sequence `1, 2, ...` from subsequent data could be misread as:
- `uart_recv_buffer[0] == 1` (UART_RX_RECORDING_STATUS)
- `uart_recv_buffer[1] == 2` (start recording)

## Fix

Two changes in `main/uart.h`:

### 1. Clear buffer before each read (line 583)

```c
while (1) {
    clearRxBuffer();  // Clear buffer before each read to prevent stale data
    const int rxBytes = uart_read_bytes(UART_NUM_2, uart_recv_buffer, cmd_buffer_size+1, 500 / portTICK_PERIOD_MS);
```

### 2. Validate packet size before acting (line 593)

```c
if (uart_recv_buffer[0] == UART_RX_RECORDING_STATUS && rxBytes >= 2){
```

Changed from just checking `uart_recv_buffer[0] == 1` to also requiring `rxBytes >= 2`, ensuring a complete packet was received before triggering recording.

## Future Improvements

If the issue persists, consider adding proper packet framing to the UART protocol:

- Start byte (e.g., `0xAA`) for synchronization
- Packet length field
- Checksum for data integrity

This would require changes on both ESP32 and Daisy Seed firmware.
