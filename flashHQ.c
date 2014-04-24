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
 *	Low-level library for the adesto AT45DB family of SPI flash chips
 */

#include <stdint.h>
#include <string.h>
#include <avr/io.h>
#include <ctype.h>
#include "spi.h"
#include "flashHQ.h"

#define FLASH_CS	    (1<<PORTB4)
#define FLASH_PORT_CS	(&PORTB)

#define FLASH_PARM_OFFSET 2 // offset from density to index

// operating paramters for the different size flash chips
flashGeometry_t flashGeom[] = {
    { 9,  3,  7, 264,   512,  128 }, //  1MB   0001 ID:00010=2  5  7 12, 6 2 16 S: 3 - 8,120,128
    { 9,  3,  7, 264,  1024,  128 }, //  2MB   0010 ID:00011=3  5  7 12, 5 3 16 S: 7 - 8,120,128
    { 9,  3,  8, 264,  2048,  256 }, //  4MB   0100 ID:00100=4  4  8 12, 4 5 17 S: 7 - 8,248,256
    { 9,  3,  8, 264,  4096,  256 }, //  8MB   1000 ID:00101=5  3  9 12, 3 4 17 S: 15 - 8,248,256
    { 10, 3,  8, 528,  4096,  256 }, // 16MB  10000 ID:00110=6  2  9 13, 2 4 18 S: 15 - 8,248,256
    { 10, 3,  7, 528,  8192,  128 }, // 32MB 100000 ID:00111=7  1 10 13, 1 6 17 S: 63 - 8,120,128
    { 9,  3, 10, 264, 32768, 1024 }, // 64MB 100000 ID:01000=8                  S: 31 - 8,1016,1024
};

static uint16_t flashCurrentBufPage = -1;   // track which page is currently loaded

// Used to indicate that the internal memory buffer is currently being used as a write
// cache.  If a function needs to use the internal buffer then it will flush the buffer
// to this page and set the it back to 0.
uint16_t flashWriteCachePage = 0;
int8_t flashId = -1;

/**
 * Return number of flash pages.
 * @return number of pages in flash
 */
uint16_t flashNumPages(void)
{
    return flashGeom[flashId].pageCount;
}


/**
 * Write the cached page to flash if it doesn't match the specified page.
 * Assumes page is already erased.
 * @param page - the new page that will be loaded
 */
bool flashFlushCache(uint16_t page)
{
    if (flashWriteCachePage && (flashWriteCachePage != page)) 
    {
        DPRINTF_P(PSTR("flashFlushCAche(): storing page\n"), flashWriteCachePage);
        flashBufStore(flashWriteCachePage);
        flashWriteCachePage = 0;
        return true;
    }

    return false;
}


/**
 * Wait for the flash to be ready
 */
void flashWaitReady(void)
{
    uint8_t res;

#ifdef DEBUG_FLASH
    uint32_t start = millis();
#endif
    pinLow(FLASH_PORT_CS, FLASH_CS);
    spiUsartTransfer(FLASH_OP_GET_STATUS);
    while (!((res=spiUsartTransfer(0)) & FLASH_STATUS_BUSY)) 
    {
    }
    pinHigh(FLASH_PORT_CS, FLASH_CS);
    DPRINTF_P(PSTR("      flashWaitReady() ready 0x%02x %dmsecs\n"), res, millis()-start);
}


/**
 * Erase entire flash
 */
void flashChipErase(void)
{
    uint8_t op[] = { FLASH_OP_CHIP_ERASE };
    flashWaitReady();
    DPRINTF_P(PSTR("writing chip erase op\n"));
    DPRINTF_P(PSTR("    %02x %02x %02x %02x\n"), op[0], op[1], op[2], op[3]);
    pinLow(FLASH_PORT_CS, FLASH_CS);
    spiUsartWrite(op, sizeof(op));
    pinHigh(FLASH_PORT_CS, FLASH_CS);
    DPRINTF_P(PSTR("waiting for erase complete\n"));
    flashWaitReady();
}


/**
 * Send a page operation code to the flash.
 * @param opcode operation to send
 * @param page the page address to send
 * @param offset the page offset to send
 */
void flashWritePageOp(uint8_t opcode, uint16_t page, uint16_t offset) 
{
    uint32_t addr = (((uint32_t)page) << flashGeom[flashId].pageOffset) | offset;

    uint8_t op[] = { opcode, ((uint8_t *)&addr)[2], ((uint8_t *)&addr)[1], ((uint8_t *)&addr)[0] };
    DPRINTF_P(PSTR("      writing op 0x%02x, page %d, offset %d\n"), opcode, page, offset);
    DPRINTF_P(PSTR("          %02x %02x %02x %02x\n"), op[0], op[1], op[2], op[3]);
    spiUsartWrite(op, sizeof(op));
}


/**
 * Execute a single page operation.
 * @param opcode operation
 * @param page the page address
 * @param offset the page offset
 */
void flashSingleOp(uint8_t op, uint16_t page, uint16_t offset) 
{
    flashWaitReady();
    pinLow(FLASH_PORT_CS, FLASH_CS);
    flashWritePageOp(op, page, offset);
    pinHigh(FLASH_PORT_CS, FLASH_CS);
}


/**
 * Check the presence of the flash
 * @retval >= 0 flash density
 * @retval -1 failure
 */
int flashCheckId(void)
{
    struct {
        uint8_t manufacturerId;
        uint8_t deviceId1;
        uint8_t deviceId2;
        uint8_t extendedInfoLen;
    } data;

    DPRINTF_P(PSTR("flashCheckId()\n"));

    pinLow(FLASH_PORT_CS, FLASH_CS);
    spiUsartTransfer(FLASH_OP_READ_DEV_ID);
    spiUsartRead((uint8_t *)&data, sizeof(data));
    pinHigh(FLASH_PORT_CS, FLASH_CS);

    DPRINTF_P(PSTR("checkId: 0x%02x 0x%02x 0x%02x 0x%02x\n"),
                data.manufacturerId, data.deviceId1,
                data.deviceId2, data.extendedInfoLen);

    /* Check ID */
    if ((data.manufacturerId != FLASH_MANUFACTURER_ID) ||
        ((data.deviceId1 & FLASH_FAMILY_MASK) != FLASH_FAMILY_ID)) {
        return -1;
    }

    // return density
    return data.deviceId1 & FLASH_DENSITY_MASK;
}


/**
 * Initialize the flash memory
 * @retval 0 success
 * @retval -1 failure
 * @note the spi usart interface must be initialised before this function is called.
 */
int flashInit(void)
{
    pinMode(FLASH_PORT_CS, FLASH_CS, OUTPUT, false);
    pinHigh(FLASH_PORT_CS, FLASH_CS);

    DPRINTF_P(PSTR("flashInit()\n"));

    /*  Check flash identification */
    flashId = flashCheckId() - FLASH_PARM_OFFSET;

    if ((flashId >= 0) && (flashId < (sizeof(flashGeom)/sizeof(flashGeom[0])))) {
        return 0;
    } else {
        return -1;
    }
}


/**
 * Load a page from flash into the internal buffer.
 * @param page the page to load
 * @retval 0 success
 * @retval -1 failed, page out of range
 */
int flashBufLoad(uint16_t page)
{
    if (page >= FLASH_NUM_PAGES) 
    {
        return -1;
    }

    if (page && (flashWriteCachePage == page)) 
    {
        // already loaded, cached
        return 0;
    }

    if (flashCurrentBufPage == page) 
    {
        // already loaded, not modified
        return 0;
    }

    flashFlushCache(page);

    flashSingleOp(FLASH_OP_BUF_LOAD, page, 0);
    flashCurrentBufPage = page;

    return 0;
}


/**
 * Read data from the internal memory buffer
 * @param datap pointer to a byte array to store the read data
 * @param offset the page offset
 * @param size the number of bytes to read
 * @note if the end of the internal buffer is reach, reading will
 *       wrap to the start of the internal buffer
 */
void flashBufRead(void *datap, uint16_t offset, uint16_t size)
{
    flashWaitReady();
    pinLow(FLASH_PORT_CS, FLASH_CS);
    flashWritePageOp(FLASH_OP_BUF_READ, 0, offset);
    spiUsartRead((uint8_t *)datap, size);
    pinHigh(FLASH_PORT_CS, FLASH_CS);
}

/**
 * Read a page from flash, bypassing the memory buffer
 * @param page the page to read
 * @param buffer pointer to the buffer to store the read data
 * @param offset the page offset
 * @param size the number of bytes to read
 * @retval 0 success
 * @retval -1 page out of range, or size is too big
 */
int flashPageRead(void *datap, uint16_t page, uint16_t offset, uint16_t size)
{
    uint8_t dummy[4];

    if ((page >= FLASH_NUM_PAGES) || (size > FLASH_PAGE_SIZE)) {
        return -1;
    }
    
    flashWaitReady();
    pinLow(FLASH_PORT_CS, FLASH_CS);
    flashWritePageOp(FLASH_OP_PAGE_READ, page, offset);
    spiUsartWrite(dummy, sizeof(dummy));   // 4 don't care bytes
    spiUsartRead((uint8_t *)datap, size);
    pinHigh(FLASH_PORT_CS, FLASH_CS);

    return 0;
}


/**
 * Contiguous data read across flash page boundaries
 * @param    datap  pointer to the buffer to store the read data
 * @param    addr   byte offset in the flash
 * @param    size   the number of bytes to read
 * @note bypasses the memory buffer
 */
void flashRawRead(void *datap, uint32_t addr, uint16_t size)
{
    uint16_t page = addr/FLASH_PAGE_SIZE;
    uint16_t offset = addr - (page*FLASH_PAGE_SIZE);

    flashWaitReady();
    pinLow(FLASH_PORT_CS, FLASH_CS);
    flashWritePageOp(FLASH_OP_READ, page, offset);
    spiUsartRead((uint8_t *)datap, size);
    pinHigh(FLASH_PORT_CS, FLASH_CS);
}


/**
 * write the contents of the internal memory buffer to a page in flash
 * @param page the page to store the buffer in
 * @retval 0 success
 * @retval -1 failed, page out of range
 */
int flashBufStore(uint16_t page)
{
    if (page >= FLASH_NUM_PAGES) 
    {
        return -1;
    }

    flashSingleOp(FLASH_OP_BUF_STORE, page, 0);
    flashCurrentBufPage = page;

    return 0;
}


/**
 * Erase a page in flash, then write the contents of the internal memory buffer to it
 * @param page the page to erase and write
 * @retval 0 success
 * @retval -1 failed, page out of range
 */
int flashBufEraseStore(uint16_t page)
{
    if (page >= FLASH_NUM_PAGES) 
    {
        return -1;
    }

    flashSingleOp(FLASH_OP_BUF_ERASE_STORE, page, 0);
    flashCurrentBufPage = page;

    return 0;
}


/**
 * erase a page of flash
 * @param page the page to erase
 * @retval 0 success
 * @retval -1 failed, page out of range
 */
int flashPageErase(uint16_t page)
{
    if (page >= FLASH_NUM_PAGES) 
    {
        return -1;
    }

    flashSingleOp(FLASH_OP_PAGE_ERASE, page, 0);

    return 0;
}


/**
 * Erase a flash sector.
 * @param    sector   the sector to erase
 * @retval   0   success
 * @retval   -1  invalid sector number 
 * @note Sector 0A and 0B must be addressed as FLASH_SECTOR_0A
 *       and FLASH_SECTOR_0B respectively.
 */
int flashSectorErase(uint16_t sector)
{
    if ((sector >= 1) && (sector < FLASH_NUM_SECTORS)) {
        sector <<= flashGeom[flashId].sector_n_offset;
    } else if ((sector == FLASH_SECTOR_0A) || (sector == FLASH_SECTOR_0B)) {
        // values for 0A and 0B were selected to map to 0 and 0x08
        sector = (sector>>8) - 1;
    } else {
        return -1;  // invalid sector
    }
    flashSingleOp(FLASH_OP_SECTOR_ERASE, sector, 0);

    return 0;
}


/**
 * Erase a block of flash memory (8 pages)
 * @param block     the block number to erase
 * @retval 0   success
 * @retval -1  failed. Block number is out of range.
 */
int flashBlockErase(uint16_t block)
{
    if (block >= (FLASH_NUM_PAGES / 8)) {
        return -1;
    }
    flashSingleOp(FLASH_OP_BLOCK_ERASE, block << 3, 0);

    return 0;
}


/**
 * Write data into the internal memory buffer
 * @param datap pointer to data to write
 * @param offset offset to start writing to in the internal memory buffer
 * @param size the number of bytes to write
 * @note if the end of the internal buffer is reached then writing will
 *       wrap to the start of the internal buffer.
 */
void flashBufWrite(void *datap, uint16_t offset, uint16_t size)
{
    if (size) {
        flashWaitReady();
        pinLow(FLASH_PORT_CS, FLASH_CS);
        flashWritePageOp(FLASH_OP_BUF_WRITE, 0, offset);
        spiUsartWrite((uint8_t *)datap, size);
        pinHigh(FLASH_PORT_CS, FLASH_CS);
        flashCurrentBufPage = -1;
    }
}

/**
 * Fill the internal memory buffer with repeating data
 * @param datap pointer to the data to write
 * @param offset offset to start writing to in the internal memory buffer
 * @param size the number of bytes to write
 * @param repeat the number of times to repeat the data write
 * @note if the end of the internal buffer is reached then writing will
 *       wrap to the start of the internal buffer.
 */
void flashBufFill(void *datap, uint16_t offset, uint16_t size, uint16_t repeat)
{
    if (size) 
    {
        flashWaitReady();
        pinLow(FLASH_PORT_CS, FLASH_CS);
        flashWritePageOp(FLASH_OP_BUF_WRITE, 0, offset);
        while (repeat--) {
            spiUsartWrite((uint8_t *)datap, size);
        }
        pinHigh(FLASH_PORT_CS, FLASH_CS);
        flashCurrentBufPage = -1;
    }
}

/**
 * Fill the internal memory buffer with repeating data
 * @param datap pointer to the data to write
 * @param offset offset to start writing to in the internal memory buffer
 * @param size the number of bytes to write
 * @param repeat the number of times to repeat the data write
 * @note if the end of the internal buffer is reached then writing will
 *       wrap to the start of the internal buffer.
 */
void flashBufSet(uint8_t value, uint16_t offset, uint16_t size)
{
    if (size) 
    {
        flashWaitReady();
        pinLow(FLASH_PORT_CS, FLASH_CS);
        flashWritePageOp(FLASH_OP_BUF_WRITE, 0, offset);
        while (size--) {
            spiUsartTransfer(value);
        }
        pinHigh(FLASH_PORT_CS, FLASH_CS);
        flashCurrentBufPage = -1;
    }
}


/**
 * Write data into the internal memory buffer with cache support.
 * @param datap pointer to data to write
 * @param page the flash page that is being cached
 * @param offset offset to start writing to in the internal memory buffer
 * @param size the number of bytes to write
 * @note if the end of the internal buffer is reached then writing will
 *       wrap to the start of the internal buffer.
 */
void flashBufWriteCached(void *datap, uint16_t page, uint16_t offset, uint16_t size)
{
    if (flashWriteCachePage != page) 
    {
        flashBufLoad(page);
        flashWriteCachePage = page;
    }

    flashBufWrite(datap, offset, size);
}


/**
 * Assumes that the page is erased already
 */
void flashBufSetCache(uint16_t page)
{
    flashFlushCache(page);
    flashWriteCachePage = page;
}


/**
 * Write data into a flash page via the internal memory buffer, performing and
 * erase for the target page.
 * @param datap pointer to data to write
 * @param page the flash page to write to
 * @param offset offset to start writing to in the flash page
 * @param size the number of bytes to write
 * @note if the end of the flash page is reached then writing will
 *       wrap to the start of the flash page.
 * @retval 0 success
 * @retval -1 page out of range, or size is too big
 */
int flashPageWrite(void *datap, uint16_t page, uint16_t offset, uint16_t size)
{

    if ((page >= FLASH_NUM_PAGES) || (size > FLASH_PAGE_SIZE)) {
        return -1;
    }

    if (size) 
    {
        flashWaitReady();
        pinLow(FLASH_PORT_CS, FLASH_CS);
        flashWritePageOp(FLASH_OP_PAGE_WRITE, page, offset);
        spiUsartWrite((uint8_t *)datap, size);
        pinHigh(FLASH_PORT_CS, FLASH_CS);

        if ((offset == 0) && (size == FLASH_PAGE_SIZE)) 
        {
            flashCurrentBufPage = page;
        }
        else 
        {
            flashCurrentBufPage = -1;
        }
    }

    return 0;
}


/**
 * dump the contents of a flash page to the USB serial debug output
 * @param page the page to dump
 */
void flashPageHexDump(uint16_t page)
{
    uint8_t buf[16];

    for (uint16_t offset=0; offset<FLASH_PAGE_SIZE; offset+=16) 
    {
        flashPageRead(buf, page, offset, sizeof(buf));
        DPRINTF_P(PSTR("%d.%04x: "), page, offset);
        for (uint8_t ind=0; ind<16; ind++) 
        {
            if ((offset+ind) >= FLASH_PAGE_SIZE)
            {
                DPRINTF_P(PSTR("     "));
            }
            else 
            {
                DPRINTF_P(PSTR("0x%02x "), buf[ind]);
            }
        }
        DPRINTF_P(PSTR(" "));
        for (uint8_t ind=0; ind<16; ind++) 
        {
            if ((offset+ind) < FLASH_PAGE_SIZE)
            {
                if (isprint(buf[ind])) 
                {
                    usb_serial_putchar(buf[ind]);
                }
                else 
                {
                    usb_serial_putchar('.');
                }
            }
            else 
            {
                break;
            }
        }
        DPRINTF_P(PSTR("\n"));
    }
}
