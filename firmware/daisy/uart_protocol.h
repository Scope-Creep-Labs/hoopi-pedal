/**
 * Hoopi Pedal - Dual-channel guitar and vocal effects processor
 * Copyright (c) 2025-2026 Scope Creep Labs LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include "daisy_seed.h"
#include <cstdint>

using namespace daisy;

// UART v2 Protocol Constants
#define UART_START_BYTE 0xAA
#define UART_MAX_DATA_LEN 4108  // OTA needs 4096 bytes + block header
#define UART_MAX_PACKET_LEN 12  // START + LEN + CMD + 8 DATA + CHECKSUM
#define UART_RX_TIMEOUT_MS 100

// Buffer sizes
#define UART_SEND_BUFF_SIZE 64   // Max response size (NAM_PACK_STATUS = 42 bytes)
#define UART_RCV_BUFF_SIZE 16    // DMA receive buffer
#define UART_RING_SIZE 8192      // Must be larger than max OTA packet

// UART RX state machine states
enum UartRxState {
    UART_RX_WAIT_START,
    UART_RX_WAIT_LEN,
    UART_RX_EXT_LEN_LO,      // Extended frame: waiting for LEN_LO
    UART_RX_EXT_LEN_HI,      // Extended frame: waiting for LEN_HI
    UART_RX_READ_PAYLOAD,
    UART_RX_WAIT_CHECKSUM,
    UART_RX_EXT_CRC_LO,      // Extended frame: waiting for CRC16 low byte
    UART_RX_EXT_CRC_HI       // Extended frame: waiting for CRC16 high byte
};

// Global UART handler
extern UartHandler uart;

// Receive buffer and payload
extern uint8_t rx_payload[UART_MAX_DATA_LEN];
extern uint16_t rx_payload_len;

// Initialize UART peripheral and DMA
void InitUart();

// Poll for UART data and parse framed packets
// Returns true if a complete valid packet was received
// Supports both standard frames (LEN <= 253) and extended frames (0xFE marker)
bool UartPollReceive();

// Send a framed packet: START + LEN + CMD + DATA + CHECKSUM
void UartSendFrame(uint8_t cmd, uint8_t* data, uint8_t data_len);

// Send a simple command with no data
void UartSendCmd(uint8_t cmd);

// Calculate XOR checksum over LEN and payload bytes
uint8_t UartCalculateChecksum(uint8_t len, uint8_t* payload);

// CRC16-CCITT for extended frames
uint16_t UartCalculateCRC16(uint8_t* data, uint16_t len);

#endif /* UART_PROTOCOL_H */
