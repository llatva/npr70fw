/**
  ******************************************************************************
  * @file    w5500_driver.h
  * @brief   W5500 Ethernet controller driver for NPR-70 FreeRTOS
  * @note    Ported from mbed-based implementation to STM32 HAL
  ******************************************************************************
  * This file is part of NPR70 modem firmware
  * Copyright (c) 2017-2020 Guillaume F. F4HDK
  * Copyright (c) 2025 FreeRTOS port
  * Licensed under GPL v3
  ******************************************************************************
  */

#ifndef W5500_DRIVER_H
#define W5500_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdint.h>
#include <stdbool.h>

/* W5500 Block addresses */
#define W5500_COMMON_REG_BLOCK          0x00
#define W5500_SOCKET_REG_BLOCK(n)       (1 + 4 * (n))
#define W5500_TX_BUFFER_BLOCK(n)        (2 + 4 * (n))
#define W5500_RX_BUFFER_BLOCK(n)        (3 + 4 * (n))

/* W5500 Common Register addresses */
#define W5500_MR                0x0000  /* Mode Register */
#define W5500_GAR0              0x0001  /* Gateway Address */
#define W5500_SUBR0             0x0005  /* Subnet Mask */
#define W5500_SHAR0             0x0009  /* Source Hardware Address (MAC) */
#define W5500_SIPR0             0x000F  /* Source IP Address */
#define W5500_INTLEVEL0         0x0013  /* Interrupt Low Level Timer */
#define W5500_IR                0x0015  /* Interrupt Register */
#define W5500_IMR               0x0016  /* Interrupt Mask */
#define W5500_SIR               0x0017  /* Socket Interrupt */
#define W5500_SIMR              0x0018  /* Socket Interrupt Mask */
#define W5500_RTR0              0x0019  /* Retry Time */
#define W5500_RCR               0x001B  /* Retry Count */
#define W5500_PHYCFGR           0x002E  /* PHY Configuration */

/* W5500 Socket Register addresses */
#define W5500_Sn_MR             0x0000  /* Socket n Mode */
#define W5500_Sn_CR             0x0001  /* Socket n Command */
#define W5500_Sn_IR             0x0002  /* Socket n Interrupt */
#define W5500_Sn_SR             0x0003  /* Socket n Status */
#define W5500_Sn_PORT0          0x0004  /* Socket n Source Port */
#define W5500_Sn_PORT1          0x0005
#define W5500_Sn_DHAR0          0x0006  /* Socket n Destination Hardware Address */
#define W5500_Sn_DIPR0          0x000C  /* Socket n Destination IP Address */
#define W5500_Sn_DPORT0         0x0010  /* Socket n Destination Port */
#define W5500_Sn_DPORT1         0x0011
#define W5500_Sn_MSSR0          0x0012  /* Socket n Max Segment Size */
#define W5500_Sn_TOS            0x0015  /* Socket n Type of Service */
#define W5500_Sn_TTL            0x0016  /* Socket n Time to Live */
#define W5500_Sn_RXBUF_SIZE     0x001E  /* Socket n RX Buffer Size */
#define W5500_Sn_TXBUF_SIZE     0x001F  /* Socket n TX Buffer Size */
#define W5500_Sn_TX_FSR0        0x0020  /* Socket n TX Free Size */
#define W5500_Sn_TX_RD0         0x0022  /* Socket n TX Read Pointer */
#define W5500_Sn_TX_WR0         0x0024  /* Socket n TX Write Pointer */
#define W5500_Sn_RX_RSR0        0x0026  /* Socket n RX Received Size */
#define W5500_Sn_RX_RD0         0x0028  /* Socket n RX Read Pointer */
#define W5500_Sn_RX_WR0         0x002A  /* Socket n RX Write Pointer */
#define W5500_Sn_IMR            0x002C  /* Socket n Interrupt Mask */
#define W5500_Sn_FRAG0          0x002D  /* Socket n Fragment Offset */
#define W5500_Sn_KPALVTR        0x002F  /* Socket n Keep Alive Timer */

/* Socket Status Register Values */
#define W5500_SOCK_CLOSED       0x00
#define W5500_SOCK_INIT         0x13
#define W5500_SOCK_LISTEN       0x14
#define W5500_SOCK_SYNSENT      0x15
#define W5500_SOCK_SYNRECV      0x16
#define W5500_SOCK_ESTABLISHED  0x17
#define W5500_SOCK_FIN_WAIT     0x18
#define W5500_SOCK_CLOSING      0x1A
#define W5500_SOCK_TIME_WAIT    0x1B
#define W5500_SOCK_CLOSE_WAIT   0x1C
#define W5500_SOCK_LAST_ACK     0x1D
#define W5500_SOCK_UDP          0x22
#define W5500_SOCK_MACRAW       0x42

/* Socket Command Register Values */
#define W5500_Sn_CR_OPEN        0x01
#define W5500_Sn_CR_LISTEN      0x02
#define W5500_Sn_CR_CONNECT     0x04
#define W5500_Sn_CR_DISCON      0x08
#define W5500_Sn_CR_CLOSE       0x10
#define W5500_Sn_CR_SEND        0x20
#define W5500_Sn_CR_SEND_MAC    0x21
#define W5500_Sn_CR_SEND_KEEP   0x22
#define W5500_Sn_CR_RECV        0x40

/* Socket Mode Register Values */
#define W5500_Sn_MR_CLOSE       0x00
#define W5500_Sn_MR_TCP         0x01
#define W5500_Sn_MR_UDP         0x02
#define W5500_Sn_MR_MACRAW      0x04

/* NPR-70 Socket Assignments */
#define W5500_SOCK_RAW          0  /* Raw socket for custom protocol */
#define W5500_SOCK_TELNET       1  /* Telnet management interface */
#define W5500_SOCK_RTP          2  /* RTP audio/data stream */
#define W5500_SOCK_DHCP         3  /* DHCP client */
#define W5500_SOCK_FDD          4  /* FDD (Full Duplex Data?) */
#define W5500_SOCK_SNMP         5  /* SNMP agent */

/* NPR-70 uses these socket definitions */
#define RAW_SOCKET              W5500_SOCKET_REG_BLOCK(W5500_SOCK_RAW)
#define TELNET_SOCKET           W5500_SOCKET_REG_BLOCK(W5500_SOCK_TELNET)
#define RTP_SOCKET              W5500_SOCKET_REG_BLOCK(W5500_SOCK_RTP)
#define DHCP_SOCKET             W5500_SOCKET_REG_BLOCK(W5500_SOCK_DHCP)
#define FDD_SOCKET              W5500_SOCKET_REG_BLOCK(W5500_SOCK_FDD)
#define SNMP_SOCKET             W5500_SOCKET_REG_BLOCK(W5500_SOCK_SNMP)

/**
  * @brief W5500 device context structure
  */
typedef struct {
    SPI_HandleTypeDef *hspi;        /* SPI handle */
    GPIO_TypeDef *cs_port;          /* CS GPIO port */
    uint16_t cs_pin;                /* CS GPIO pin */
    GPIO_TypeDef *int_port;         /* Interrupt GPIO port */
    uint16_t int_pin;               /* Interrupt GPIO pin */
    SemaphoreHandle_t spi_mutex;    /* SPI bus mutex (shared with SRAM) */
    uint8_t sock_interrupt;         /* Socket interrupt status */
} W5500_Context_t;

/* Function prototypes */

/**
  * @brief  Initialize W5500 device
  * @param  ctx: Pointer to W5500 context
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_Init(W5500_Context_t *ctx);

/**
  * @brief  Read multiple bytes from W5500
  * @param  ctx: Pointer to W5500 context
  * @param  addr: Register address (16-bit)
  * @param  block: Block select byte
  * @param  data: Pointer to receive buffer
  * @param  len: Number of bytes to read
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_ReadData(W5500_Context_t *ctx, uint16_t addr, uint8_t block, 
                                  uint8_t *data, uint16_t len);

/**
  * @brief  Write multiple bytes to W5500
  * @param  ctx: Pointer to W5500 context
  * @param  addr: Register address (16-bit)
  * @param  block: Block select byte
  * @param  data: Pointer to transmit buffer
  * @param  len: Number of bytes to write
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_WriteData(W5500_Context_t *ctx, uint16_t addr, uint8_t block,
                                   const uint8_t *data, uint16_t len);

/**
  * @brief  Read single byte from W5500
  * @param  ctx: Pointer to W5500 context
  * @param  addr: Register address (16-bit)
  * @param  block: Block select byte
  * @retval Byte value read
  */
uint8_t W5500_ReadByte(W5500_Context_t *ctx, uint16_t addr, uint8_t block);

/**
  * @brief  Write single byte to W5500
  * @param  ctx: Pointer to W5500 context
  * @param  addr: Register address (16-bit)
  * @param  block: Block select byte
  * @param  data: Byte value to write
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_WriteByte(W5500_Context_t *ctx, uint16_t addr, uint8_t block, uint8_t data);

/**
  * @brief  Read 16-bit word from W5500
  * @param  ctx: Pointer to W5500 context
  * @param  addr: Register address (16-bit)
  * @param  block: Block select byte
  * @retval 16-bit word value
  */
uint16_t W5500_ReadWord(W5500_Context_t *ctx, uint16_t addr, uint8_t block);

/**
  * @brief  Write 16-bit word to W5500
  * @param  ctx: Pointer to W5500 context
  * @param  addr: Register address (16-bit)
  * @param  block: Block select byte
  * @param  data: 16-bit word value to write
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_WriteWord(W5500_Context_t *ctx, uint16_t addr, uint8_t block, uint16_t data);

/**
  * @brief  Get received data size for a socket
  * @param  ctx: Pointer to W5500 context
  * @param  sock: Socket number (0-7)
  * @retval Number of bytes received
  */
uint16_t W5500_GetRxSize(W5500_Context_t *ctx, uint8_t sock);

/**
  * @brief  Get free TX buffer size for a socket
  * @param  ctx: Pointer to W5500 context
  * @param  sock: Socket number (0-7)
  * @retval Number of bytes free in TX buffer
  */
uint16_t W5500_GetTxFreeSize(W5500_Context_t *ctx, uint8_t sock);

/**
  * @brief  Read from socket RX buffer
  * @param  ctx: Pointer to W5500 context
  * @param  sock: Socket number (0-7)
  * @param  data: Pointer to receive buffer
  * @param  len: Number of bytes to read
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_RecvData(W5500_Context_t *ctx, uint8_t sock, uint8_t *data, uint16_t len);

/**
  * @brief  Write to socket TX buffer
  * @param  ctx: Pointer to W5500 context
  * @param  sock: Socket number (0-7)
  * @param  data: Pointer to transmit buffer
  * @param  len: Number of bytes to write
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_SendData(W5500_Context_t *ctx, uint8_t sock, const uint8_t *data, uint16_t len);

/**
  * @brief  Execute socket command
  * @param  ctx: Pointer to W5500 context
  * @param  sock: Socket number (0-7)
  * @param  cmd: Command byte (W5500_Sn_CR_xxx)
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_ExecCommand(W5500_Context_t *ctx, uint8_t sock, uint8_t cmd);

/**
  * @brief  Get socket status
  * @param  ctx: Pointer to W5500 context
  * @param  sock: Socket number (0-7)
  * @retval Socket status (W5500_SOCK_xxx)
  */
uint8_t W5500_GetSocketStatus(W5500_Context_t *ctx, uint8_t sock);

/**
  * @brief  PHY power off for 5 seconds (reset)
  * @param  ctx: Pointer to W5500 context
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_PhyReset(W5500_Context_t *ctx);

/**
  * @brief  Initialize UDP socket
  * @param  ctx: Pointer to W5500 context
  * @param  sock: Socket number (0-7)
  * @param  port: Local port number
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_InitUDPSocket(W5500_Context_t *ctx, uint8_t sock, uint16_t port);

/**
  * @brief  Initialize TCP server socket
  * @param  ctx: Pointer to W5500 context
  * @param  sock: Socket number (0-7)
  * @param  port: Local port number
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_InitTCPServerSocket(W5500_Context_t *ctx, uint8_t sock, uint16_t port);

/**
  * @brief  Configure all application sockets (DHCP, SNMP, Telnet)
  * @param  ctx: Pointer to W5500 context
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_ConfigureAppSockets(W5500_Context_t *ctx);

/**
  * @brief  Read UDP packet from socket (with source IP and port)
  * @param  ctx: Pointer to W5500 context
  * @param  sock: Socket number (0-7)
  * @param  data: Pointer to receive buffer
  * @param  len: Maximum buffer size
  * @param  src_ip: Pointer to store source IP (4 bytes)
  * @param  src_port: Pointer to store source port
  * @retval Number of bytes read (payload only, excluding 8-byte header)
  */
uint16_t W5500_ReadUDP(W5500_Context_t *ctx, uint8_t sock, uint8_t *data, uint16_t len,
                       uint32_t *src_ip, uint16_t *src_port);

/**
  * @brief  Send UDP packet to destination IP and port
  * @param  ctx: Pointer to W5500 context
  * @param  sock: Socket number (0-7)
  * @param  data: Pointer to transmit buffer
  * @param  len: Number of bytes to send
  * @param  dst_ip: Destination IP address (32-bit)
  * @param  dst_port: Destination port
  * @retval HAL status
  */
HAL_StatusTypeDef W5500_SendUDP(W5500_Context_t *ctx, uint8_t sock, const uint8_t *data, 
                                 uint16_t len, uint32_t dst_ip, uint16_t dst_port);

#ifdef __cplusplus
}
#endif

#endif /* W5500_DRIVER_H */
