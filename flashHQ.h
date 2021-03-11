/*-
 * Copyright (c) 2014 Darran Hunt (darran [at] hunt dot net dot nz)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*	
 * A low-level library for the adesto AT45DB family of SPI flash chips
 */


#ifndef FLASHHQ_H_
#define FLASHHQ_H_

#include <Arduino.h>
#include <SPI.h>
//#include <stdbool.h>
//#include <stdint.h>
//#include <string.h>
//#include <avr/io.h>
#include <ctype.h>


#undef DEBUG_FLASH
#ifdef DEBUG_FLASH
#define DPRINTF_P(args...) printf_P(args)
#else
#define DPRINTF_P(args...)
#endif

#define pinLow(port, pin)	*(port) &= ~(pin)
#define pinHigh(port, pin)	*(port) |= pin

#define FLASH_NUM_PAGES           (flashGeom[flashId].pageCount)
#define FLASH_PAGE_SIZE           (flashGeom[flashId].pageSize)
#define FLASH_SECTOR_SIZE         (flashGeom[flashId].sectorSize)
#define FLASH_NUM_SECTORS         (FLASH_NUM_PAGES / FLASH_SECTOR_SIZE)

#define FLASH_FAMILY_MASK         0xE0	// bitmask for family field in flash device ID
#define FLASH_FAMILY_ID           0x20  // The flash family supported
#define FLASH_MANUFACTURER_ID     0x1F
#define FLASH_DENSITY_MASK        0x1F  // bitmask for the flash density in the device ID

#define FLASH_OP_PAGE_READ        0xD2	// read one page
#define FLASH_OP_PAGE_WRITE	  0x82	// write one flash page via memory buffer with auto erase
#define FLASH_OP_PAGE_ERASE	  0x81	// erase one flash page
#define FLASH_OP_READ             0x03	// random access continuous read (low freq)
#define FLASH_OP_BUF_LOAD         0x53	// load memory buffer from page
#define FLASH_OP_BUF_READ         0xD1	// read from the memory buffer (low freq)
#define FLASH_OP_BUF_CMP          0x60	// compare memory buffer to page
#define FLASH_OP_BUF_WRITE        0x84	// write to the memory buffer
#define FLASH_OP_BUF_ERASE_STORE  0x83	// write the buffer to a flash page, no erase
#define FLASH_OP_BUF_STORE        0x88	// write the buffer to a flash page, no erase
#define FLASH_OP_CHIP_ERASE       0xC7, 0x94, 0x80, 0x9A	// erase entire chip
#define FLASH_OP_GET_STATUS       0xD7	// Read status
#define FLASH_OP_SECTOR_ERASE     0x7C  // Erase a sector
#define FLASH_OP_BLOCK_ERASE      0x50  // Erase a block
#define	FLASH_OP_READ_DEV_ID      0x9F  // Read Manufacturing and Device ID

#define FLASH_STATUS_BUSY         (1<<7)  // flash status busy bit

#define FLASH_SECTOR_0A           0x0800 // Internal identifier for Sector 0a operations
#define FLASH_SECTOR_0B		  0x1000 // Internal identifier for Sector 0b operations

// Flash geometry
typedef struct {
    uint8_t pageOffset;
    uint8_t sector_0_offset;
    uint8_t sector_n_offset;
    uint16_t pageSize;
    uint16_t pageCount;
    uint16_t sectorSize;;
} flashGeometry_t;

extern int8_t flashId;
extern flashGeometry_t flashGeom[];

int flashInit(void);
uint16_t flashNumPages(void);
int flashCheckId(void);
int flashBufLoad(uint16_t page);
void flashBufRead(void *datap, uint16_t offset, uint16_t size);
void flashRawRead(void *datap, uint32_t addr, uint16_t size);
int flashPageRead(void *datap, uint16_t page, uint16_t offset, uint16_t size);
int flashBufStore(uint16_t page);
int flashBufEraseStore(uint16_t page);

void flashChipErase(void);
int flashPageErase(uint16_t page);
int flashBlockErase(uint16_t block);
int flashSectorErase(uint16_t sector);

void flashBufWrite(const void *datap, uint16_t offset, uint16_t size);
void flashBufFill(void *datap, uint16_t offset, uint16_t size, uint16_t repeat);
void flashBufSet(uint8_t value, uint16_t offset, uint16_t size);

void flashBufWriteCached(void *datap, uint16_t page, uint16_t offset, uint16_t size);
void flashBufSetCache(uint16_t page);
int flashPageWrite(void *datap, uint16_t page, uint16_t offset, uint16_t size);
bool flashFlushCache(uint16_t page);

void flashPageHexDump(uint16_t page);
#endif
