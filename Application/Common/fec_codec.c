/**
  ******************************************************************************
  * @file    fec_codec.c
  * @brief   Forward Error Correction codec implementation
  * @note    Ported from source/L1L2_radio.cpp and source/global_variables.cpp
  ******************************************************************************
  */

#include "fec_codec.h"
#include "app_common.h"
#include <string.h>

/* Parity bit elaboration table: add even parity to 7-bit value */
/* Input: 7-bit value (0-127), Output: 0x00 (even parity) or 0x80 (odd count, set bit 7) */
const uint8_t parity_bit_elab[128] = {
	0x00,0x80,0x80,0x00,0x80,0x00,0x00,0x80,0x80,0x00,0x00,0x80,0x00,0x80,0x80,0x00,
	0x80,0x00,0x00,0x80,0x00,0x80,0x80,0x00,0x00,0x80,0x80,0x00,0x80,0x00,0x00,0x80,
	0x80,0x00,0x00,0x80,0x00,0x80,0x80,0x00,0x00,0x80,0x80,0x00,0x80,0x00,0x00,0x80,
	0x00,0x80,0x80,0x00,0x80,0x00,0x00,0x80,0x80,0x00,0x00,0x80,0x00,0x80,0x80,0x00,
	0x80,0x00,0x00,0x80,0x00,0x80,0x80,0x00,0x00,0x80,0x80,0x00,0x80,0x00,0x00,0x80,
	0x00,0x80,0x80,0x00,0x80,0x00,0x00,0x80,0x80,0x00,0x00,0x80,0x00,0x80,0x80,0x00,
	0x00,0x80,0x80,0x00,0x80,0x00,0x00,0x80,0x80,0x00,0x00,0x80,0x00,0x80,0x80,0x00,
	0x80,0x00,0x00,0x80,0x00,0x80,0x80,0x00,0x00,0x80,0x80,0x00,0x80,0x00,0x00,0x80
};

/* Parity bit check table: 1 if even parity OK, 0 if error */
const uint8_t parity_bit_check[256] = {
	1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
	0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
	0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
	1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
	0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,
	1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
	1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,
	0,1,1,0,1,0,0,1,1,0,0,1,0,1,1,0,1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1
};

/**
 * @brief Calculate encoded size
 */
int FEC_SizeWithEncoding(int size_wo_FEC)
{
	int size_w_FEC;
	size_w_FEC = size_wo_FEC / 3;
	if ((size_wo_FEC % 3) > 0) {
		size_w_FEC++;
	}
	size_w_FEC = 4 * size_w_FEC + 4;
	return size_w_FEC;
}

/**
 * @brief Encode data with FEC (4,3 code)
 * 
 * Algorithm:
 *   1. Divide input into 3 equal-sized blocks (pad last block if needed)
 *   2. Create 4th block as XOR of blocks 1, 2, 3
 *   3. Compute CRC (XOR of all bytes) for each block
 *   4. Output: [block1][CRC1][block2][CRC2][block3][CRC3][block4][CRC4]
 */
int FEC_Encode(const uint8_t *data_in, uint8_t *data_out, int size_in)
{
	int size_out;
	int size_single_bloc;
	int size_single_bloc_pl1;  /* size_single_bloc + 1 (for CRC byte) */
	uint8_t CRC_1, CRC_2, CRC_3, CRC_4;
	uint8_t data_field_1, data_field_2, data_field_3, data_field_4;
	int i;
	
	if (data_in == NULL || data_out == NULL || size_in <= 0) {
		return 0;
	}
	
	CRC_1 = 0;
	CRC_2 = 0;
	CRC_3 = 0;
	CRC_4 = 0;
	
	/* Calculate block size (input split into 3 equal parts, round up) */
	size_single_bloc = size_in / 3;
	if (size_in % 3) {
		size_single_bloc++;
	}
	size_single_bloc_pl1 = size_single_bloc + 1;
	size_out = 4 * size_single_bloc_pl1;
	
	/* Process each byte position across the 3 input blocks */
	for (i = 0; i < size_single_bloc; i++) {
		/* Read from the 3 input blocks (with bounds checking) */
		data_field_1 = (i < size_in) ? data_in[i] : 0;
		data_field_2 = ((size_single_bloc + i) < size_in) ? data_in[size_single_bloc + i] : 0;
		data_field_3 = ((2 * size_single_bloc + i) < size_in) ? data_in[2 * size_single_bloc + i] : 0;
		
		/* 4th field is XOR of first 3 (for error recovery) */
		data_field_4 = data_field_1 ^ data_field_2 ^ data_field_3;
		
		/* Accumulate CRC (simple XOR of all bytes in each field) */
		CRC_1 ^= data_field_1;
		CRC_2 ^= data_field_2;
		CRC_3 ^= data_field_3;
		CRC_4 ^= data_field_4;
		
		/* Write output: 4 blocks, each followed by its CRC */
		data_out[i] = data_field_1;
		data_out[size_single_bloc_pl1 + i] = data_field_2;
		data_out[2 * size_single_bloc_pl1 + i] = data_field_3;
		data_out[3 * size_single_bloc_pl1 + i] = data_field_4;
	}
	
	/* Append CRC bytes at the end of each block */
	data_out[size_single_bloc] = CRC_1;
	data_out[2 * size_single_bloc + 1] = CRC_2;
	data_out[3 * size_single_bloc + 2] = CRC_3;
	data_out[4 * size_single_bloc + 3] = CRC_4;
	
	return size_out;
}

/**
 * @brief Decode FEC-encoded data from RX FIFO
 * 
 * Reads from global RX_FIFO_data starting at RX_FIFO_RD_point.
 * Can recover from single-field corruption using XOR reconstruction.
 * Advances RX_FIFO_RD_point on success.
 */
int FEC_Decode(uint8_t *data_out, int size_in, uint32_t *micro_BER)
{
	int size_out;
	int size_single_bloc;
	int size_single_bloc_pl1;
	uint8_t CRC_check_1, CRC_check_2, CRC_check_3, CRC_check_4;
	uint8_t data_temp;
	uint32_t nb_errors;
	int i;
	
	if (data_out == NULL || micro_BER == NULL || size_in < 4) {
		if (micro_BER) *micro_BER = 0;
		return 0;
	}
	
	/* Each encoded block has size_single_bloc data bytes + 1 CRC byte */
	size_single_bloc_pl1 = size_in / 4;
	size_single_bloc = size_single_bloc_pl1 - 1;
	
	/* Compute CRC for each of the 4 fields */
	CRC_check_1 = 0;
	CRC_check_2 = 0;
	CRC_check_3 = 0;
	CRC_check_4 = 0;
	
	for (i = 0; i < size_single_bloc_pl1; i++) {
		CRC_check_1 ^= RX_FIFO_data[(RX_FIFO_RD_point + i) & RX_FIFO_MASK];
		CRC_check_2 ^= RX_FIFO_data[(RX_FIFO_RD_point + size_single_bloc_pl1 + i) & RX_FIFO_MASK];
		CRC_check_3 ^= RX_FIFO_data[(RX_FIFO_RD_point + 2 * size_single_bloc_pl1 + i) & RX_FIFO_MASK];
		CRC_check_4 ^= RX_FIFO_data[(RX_FIFO_RD_point + 3 * size_single_bloc_pl1 + i) & RX_FIFO_MASK];
	}
	
	/* Count corrupted fields */
	nb_errors = 0;
	if (CRC_check_1 != 0) nb_errors++;
	if (CRC_check_2 != 0) nb_errors++;
	if (CRC_check_3 != 0) nb_errors++;
	if (CRC_check_4 != 0) nb_errors++;
	
	*micro_BER = nb_errors;
	
	/* Can only recover if ≤1 field is corrupted */
	if (nb_errors > 1) {
		size_out = 0;  /* Unrecoverable error */
	} else {
		size_out = 3 * size_single_bloc;
		
		/* FIELD 1: decode or reconstruct */
		if (CRC_check_1 == 0) {
			/* Field 1 OK - copy directly */
			for (i = 0; i < size_single_bloc; i++) {
				data_out[i] = RX_FIFO_data[(RX_FIFO_RD_point + i) & RX_FIFO_MASK];
			}
		} else {
			/* Field 1 corrupted - reconstruct from fields 2, 3, 4 */
			for (i = 0; i < size_single_bloc; i++) {
				data_temp  = RX_FIFO_data[(RX_FIFO_RD_point + size_single_bloc_pl1 + i) & RX_FIFO_MASK];
				data_temp ^= RX_FIFO_data[(RX_FIFO_RD_point + 2 * size_single_bloc_pl1 + i) & RX_FIFO_MASK];
				data_temp ^= RX_FIFO_data[(RX_FIFO_RD_point + 3 * size_single_bloc_pl1 + i) & RX_FIFO_MASK];
				data_out[i] = data_temp;
			}
		}
		
		/* FIELD 2: decode or reconstruct */
		if (CRC_check_2 == 0) {
			/* Field 2 OK */
			for (i = 0; i < size_single_bloc; i++) {
				data_out[size_single_bloc + i] = RX_FIFO_data[(RX_FIFO_RD_point + size_single_bloc_pl1 + i) & RX_FIFO_MASK];
			}
		} else {
			/* Field 2 corrupted - reconstruct from fields 1, 3, 4 */
			for (i = 0; i < size_single_bloc; i++) {
				data_temp  = RX_FIFO_data[(RX_FIFO_RD_point + i) & RX_FIFO_MASK];
				data_temp ^= RX_FIFO_data[(RX_FIFO_RD_point + 2 * size_single_bloc_pl1 + i) & RX_FIFO_MASK];
				data_temp ^= RX_FIFO_data[(RX_FIFO_RD_point + 3 * size_single_bloc_pl1 + i) & RX_FIFO_MASK];
				data_out[size_single_bloc + i] = data_temp;
			}
		}
		
		/* FIELD 3: decode or reconstruct */
		if (CRC_check_3 == 0) {
			/* Field 3 OK */
			for (i = 0; i < size_single_bloc; i++) {
				data_out[2 * size_single_bloc + i] = RX_FIFO_data[(RX_FIFO_RD_point + 2 * size_single_bloc_pl1 + i) & RX_FIFO_MASK];
			}
		} else {
			/* Field 3 corrupted - reconstruct from fields 1, 2, 4 */
			for (i = 0; i < size_single_bloc; i++) {
				data_temp  = RX_FIFO_data[(RX_FIFO_RD_point + i) & RX_FIFO_MASK];
				data_temp ^= RX_FIFO_data[(RX_FIFO_RD_point + size_single_bloc_pl1 + i) & RX_FIFO_MASK];
				data_temp ^= RX_FIFO_data[(RX_FIFO_RD_point + 3 * size_single_bloc_pl1 + i) & RX_FIFO_MASK];
				data_out[2 * size_single_bloc + i] = data_temp;
			}
		}
	}
	
	/* Advance read pointer */
	RX_FIFO_RD_point = RX_FIFO_RD_point + size_in;
	
	return size_out;
}
