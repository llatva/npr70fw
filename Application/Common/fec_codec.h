/**
  ******************************************************************************
  * @file    fec_codec.h
  * @brief   Forward Error Correction codec for NPR-70 radio
  * @note    Ported from source/L1L2_radio.cpp FEC functions
  ******************************************************************************
  * Original mbed implementation (c) 2017-2020 Guillaume F. F4HDK
  * FreeRTOS port (c) 2026 Lasse OH3HZB
  * Licensed under GPL v3
  ******************************************************************************
  */

#ifndef FEC_CODEC_H
#define FEC_CODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Parity bit tables for 7-bit values with 8th bit as even parity */
extern const uint8_t parity_bit_elab[128];   /* Add parity bit: input 7 bits -> output bit 7 (0x80 or 0x00) */
extern const uint8_t parity_bit_check[256];  /* Check parity: returns 1 if even parity OK, 0 if error */

/**
 * @brief Calculate size with FEC encoding
 * @param size_wo_FEC Size without FEC
 * @return Size with FEC encoding
 */
int FEC_SizeWithEncoding(int size_wo_FEC);

/**
 * @brief Encode data with FEC (4,3 code with CRC per field)
 * @param data_in Input data (must be at least size_in bytes)
 * @param data_out Output buffer (must be at least FEC_SizeWithEncoding(size_in) bytes)
 * @param size_in Input data size
 * @return Encoded size
 * 
 * @note Algorithm:
 *   - Split input into 3 equal blocks (pad last block if needed)
 *   - Create 4th block as XOR of first 3
 *   - Append CRC byte to each block
 *   - Output is 4*(ceil(size_in/3)+1) bytes
 */
int FEC_Encode(const uint8_t *data_in, uint8_t *data_out, int size_in);

/**
 * @brief Decode FEC-encoded data from RX FIFO
 * @param data_out Output decoded data buffer
 * @param size_in Encoded data size in FIFO
 * @param micro_BER Output: number of corrupted fields (0-4)
 * @return Decoded size (0 if unrecoverable error >1 field corrupted)
 * 
 * @note Reads from global RX_FIFO_data starting at RX_FIFO_RD_point
 *       Advances RX_FIFO_RD_point by size_in on success
 *       Can recover from single field corruption using XOR reconstruction
 */
int FEC_Decode(uint8_t *data_out, int size_in, uint32_t *micro_BER);

#ifdef __cplusplus
}
#endif

#endif /* FEC_CODEC_H */
