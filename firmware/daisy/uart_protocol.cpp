/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#include "uart_protocol.h"
#include "pin_defs.h"

using namespace daisy;

// Global UART handler
UartHandler uart;

// DMA and send buffers
static uint8_t DMA_BUFFER_MEM_SECTION send_buffer[UART_SEND_BUFF_SIZE];
static uint8_t DMA_BUFFER_MEM_SECTION dma_recv_buffer[UART_RCV_BUFF_SIZE];

// Receive buffer and payload
uint8_t rx_payload[UART_MAX_DATA_LEN];
uint16_t rx_payload_len = 0;

// Ring buffer for DMA received data
static uint8_t uart_ring_buffer[UART_RING_SIZE];
static volatile uint16_t uart_ring_head = 0;  // Written by DMA callback
static uint16_t uart_ring_tail = 0;           // Read by main loop

// State machine state
static UartRxState uart_rx_state = UART_RX_WAIT_START;
static uint16_t uart_rx_len = 0;
static uint16_t uart_rx_idx = 0;
static uint32_t uart_rx_start_time = 0;
static bool uart_rx_extended = false;
static uint16_t uart_rx_crc16 = 0;

// Calculate XOR checksum over LEN and payload bytes
uint8_t UartCalculateChecksum(uint8_t len, uint8_t* payload) {
    uint8_t checksum = len;
    for (int i = 0; i < len; i++) {
        checksum ^= payload[i];
    }
    return checksum;
}

// CRC16-CCITT for extended frames
uint16_t UartCalculateCRC16(uint8_t* data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
        }
    }
    return crc;
}

// Send a framed packet: START + LEN + CMD + DATA + CHECKSUM
void UartSendFrame(uint8_t cmd, uint8_t* data, uint8_t data_len) {
    uint8_t len = 1 + data_len;  // CMD + DATA
    uint8_t payload[UART_MAX_DATA_LEN + 1];  // +1 for CMD byte

    payload[0] = cmd;
    for (int i = 0; i < data_len && i < UART_MAX_DATA_LEN; i++) {
        payload[i + 1] = data[i];
    }

    send_buffer[0] = UART_START_BYTE;
    send_buffer[1] = len;
    for (int i = 0; i < len; i++) {
        send_buffer[2 + i] = payload[i];
    }
    send_buffer[2 + len] = UartCalculateChecksum(len, payload);

    int total_len = 3 + len;  // START + LEN + payload + CHECKSUM
    uart.BlockingTransmit(send_buffer, total_len);
}

// Send a simple command with no data
void UartSendCmd(uint8_t cmd) {
    UartSendFrame(cmd, nullptr, 0);
}

// DMA circular receive callback - called on half/full transfer and idle
static void UartDmaCallback(uint8_t* data, size_t size, void* context, UartHandler::Result result) {
    if (result == UartHandler::Result::OK && size > 0) {
        // Copy received bytes to ring buffer
        for (size_t i = 0; i < size; i++) {
            uint16_t next_head = (uart_ring_head + 1) % UART_RING_SIZE;
            if (next_head != uart_ring_tail) {  // Don't overflow
                uart_ring_buffer[uart_ring_head] = data[i];
                uart_ring_head = next_head;
            }
        }
    }
}

void InitUart() {
    // Configure the Uart Peripheral
    // Using UART4 on D11/D12 to keep D29/D30 free for USB-C (DFU recovery)
    UartHandler::Config uart_conf;
    uart_conf.periph        = UartHandler::Config::Peripheral::UART_4;
    uart_conf.mode          = UartHandler::Config::Mode::TX_RX;
    uart_conf.pin_config.tx = UART_TX;
    uart_conf.pin_config.rx = UART_RX;
    uart_conf.baudrate      = 460800;

    // Clear buffers
    for (int i = 0; i < UART_RCV_BUFF_SIZE; i++) {
        dma_recv_buffer[i] = 0;
    }
    for (int i = 0; i < UART_SEND_BUFF_SIZE; i++) {
        send_buffer[i] = 0;
    }
    for (int i = 0; i < UART_MAX_DATA_LEN; i++) {
        rx_payload[i] = 0;
    }
    uart_ring_head = 0;
    uart_ring_tail = 0;

    uart.Init(uart_conf);
    uart_rx_state = UART_RX_WAIT_START;

    // Start DMA listening mode for continuous reception
    uart.DmaListenStart(dma_recv_buffer, UART_RCV_BUFF_SIZE, UartDmaCallback, nullptr);
}

// Check if ring buffer has data available
static inline bool UartRingAvailable() {
    return uart_ring_head != uart_ring_tail;
}

// Read one byte from ring buffer (call only if UartRingAvailable() is true)
static inline uint8_t UartRingRead() {
    uint8_t byte = uart_ring_buffer[uart_ring_tail];
    uart_ring_tail = (uart_ring_tail + 1) % UART_RING_SIZE;
    return byte;
}

// Poll for UART data and parse framed packets
bool UartPollReceive() {
    uint8_t byte;

    // Check for timeout - reset state if we've been waiting too long
    if (uart_rx_state != UART_RX_WAIT_START) {
        uint32_t timeout = uart_rx_extended ? 2000 : UART_RX_TIMEOUT_MS;  // Longer timeout for extended
        if (System::GetNow() - uart_rx_start_time > timeout) {
            uart_rx_state = UART_RX_WAIT_START;
            uart_rx_extended = false;
        }
    }

    // Process all available bytes from ring buffer
    while (UartRingAvailable()) {
        byte = UartRingRead();

        switch (uart_rx_state) {
            case UART_RX_WAIT_START:
                if (byte == UART_START_BYTE) {
                    uart_rx_state = UART_RX_WAIT_LEN;
                    uart_rx_start_time = System::GetNow();
                    uart_rx_extended = false;
                }
                break;

            case UART_RX_WAIT_LEN:
                if (byte == 0xFE) {
                    // Extended frame marker
                    uart_rx_extended = true;
                    uart_rx_state = UART_RX_EXT_LEN_LO;
                } else if (byte >= 1 && byte <= 253) {  // Standard frame
                    uart_rx_len = byte;
                    uart_rx_idx = 0;
                    uart_rx_state = UART_RX_READ_PAYLOAD;
                } else {
                    // Invalid length, reset
                    uart_rx_state = UART_RX_WAIT_START;
                }
                break;

            case UART_RX_EXT_LEN_LO:
                uart_rx_len = byte;  // Low byte of length
                uart_rx_state = UART_RX_EXT_LEN_HI;
                break;

            case UART_RX_EXT_LEN_HI:
                uart_rx_len |= (uint16_t)byte << 8;  // High byte of length
                // Length includes CMD + DATA + CRC16(2), so payload is len-2
                if (uart_rx_len >= 3 && uart_rx_len <= UART_MAX_DATA_LEN + 2) {
                    uart_rx_idx = 0;
                    uart_rx_state = UART_RX_READ_PAYLOAD;
                } else {
                    uart_rx_state = UART_RX_WAIT_START;
                    uart_rx_extended = false;
                }
                break;

            case UART_RX_READ_PAYLOAD:
                rx_payload[uart_rx_idx++] = byte;
                if (uart_rx_extended) {
                    // Extended frame: len includes CRC16, so payload ends at len-2
                    if (uart_rx_idx >= uart_rx_len - 2) {
                        uart_rx_state = UART_RX_EXT_CRC_LO;
                    }
                } else {
                    // Standard frame
                    if (uart_rx_idx >= uart_rx_len) {
                        uart_rx_state = UART_RX_WAIT_CHECKSUM;
                    }
                }
                break;

            case UART_RX_WAIT_CHECKSUM:
                {
                    uint8_t expected = UartCalculateChecksum((uint8_t)uart_rx_len, rx_payload);
                    if (byte == expected) {
                        // Valid packet received
                        rx_payload_len = uart_rx_len;
                        uart_rx_state = UART_RX_WAIT_START;
                        return true;
                    } else {
                        // Checksum mismatch, discard
                        uart_rx_state = UART_RX_WAIT_START;
                    }
                }
                break;

            case UART_RX_EXT_CRC_LO:
                uart_rx_crc16 = byte;  // Low byte of CRC16
                uart_rx_state = UART_RX_EXT_CRC_HI;
                break;

            case UART_RX_EXT_CRC_HI:
                uart_rx_crc16 |= (uint16_t)byte << 8;  // High byte of CRC16
                {
                    uint16_t payload_len = uart_rx_len - 2;  // Exclude CRC16 from length
                    uint16_t expected = UartCalculateCRC16(rx_payload, payload_len);
                    if (uart_rx_crc16 == expected) {
                        // Valid extended packet received
                        rx_payload_len = payload_len;
                        uart_rx_state = UART_RX_WAIT_START;
                        uart_rx_extended = false;
                        return true;
                    } else {
                        // CRC mismatch, discard
                        uart_rx_state = UART_RX_WAIT_START;
                        uart_rx_extended = false;
                    }
                }
                break;
        }
    }

    return false;
}
