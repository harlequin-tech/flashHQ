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
 *	A simple flash file system for the adesto AT45DB family of SPI flash chips
 */

#include <stddef.h>
#include <alloca.h>
#include <stdint.h>
#include <string.h>
#include <avr/io.h>
#include <math.h>
#include "flashfile.h"

#define max(x,y) (x > y ? x : y)

bool flashMapNodeAvailable(flashNodeMap_t *map, uint16_t node)
{
    return (map->free[node>>3] & (1 << (7-(node & 7)))) != 0;
}

void flashFormat(void)
{
    DPRINTF_P(PSTR("Erasing chip\n"));
    flashChipErase();

    uint16_t count;
    uint16_t mapPage;
    uint8_t used = 0xFF >> (FLASH_MAP_PAGE_COUNT+1);   // 1 for the directory entry

    flashBufSet(used, 0, 1);    // flag map pages as used
    flashBufSet(0xFF, 1, FLASH_PAGE_SIZE-1);
    for (mapPage=0, count = FLASH_NUM_PAGES/8; count > FLASH_PAGE_SIZE; mapPage++,count-=FLASH_PAGE_SIZE) {
        if (mapPage == 1) {
            flashBufSet(0xFF, 0, 1);
        }
        flashBufStore(mapPage);
    }
    flashBufSet(0, count, FLASH_PAGE_SIZE-count);
    flashBufStore(mapPage);
}


uint16_t flashFindFile(char *filename, flashDirEntry_t *dir, uint16_t *lastPage)
{
    uint32_t page = FLASH_DIR_START_PAGE;
    uint8_t len = strlen(filename);
    flashDirEntry_t *entry = (flashDirEntry_t *)0;
    char *str = alloca(len+1);

    if (lastPage) {
        *lastPage = 0;
    }

    do {
        flashPageRead(dir, page, 0, sizeof(*dir));
        DPRINTF_P(PSTR("flashFindFile(): dir page %d, next=%d (size=%d)\n"), page, dir->nextEntryPage,
                sizeof(*dir));

        if (dir->nextEntryPage == 0xFFFF) {
            // no directory entries...
            if (lastPage) {
                *lastPage = page;
            }
            return 0;
        }
        flashPageRead(str, page, (uint16_t)&entry->name, len + 1);
        if (strcmp(filename,str) == 0) {
            return page;
        }
        if (lastPage) {
            *lastPage = page;
        }
        page = dir->nextEntryPage;
    } while (page != 0);

    return 0;
}

int flashOpen(char *filename, flashFile_t *filep)
{
    flashDirEntry_t dir;
    uint16_t page = flashFindFile(filename, &dir, NULL);

    if (page != 0) {
        flashPageRead(&dir, page, 0, sizeof(dir));
        filep->startNode = dir.startNode;
        filep->endNode = dir.endNode;
        filep->size = dir.size;
        filep->offset = 0;
        filep->curNode = 0;
        filep->eof = false;
        return 0;
    }

    return -1;
}


int flashSeek(flashFile_t *filep, uint16_t filepos)
{
    return 0;
}

int flashRead(flashFile_t *filep, uint8_t *buffer, uint16_t size)
{
    uint16_t rsize = size;
    uint16_t dsize = 0;
    uint16_t start;

    if (filep->eof) {
        return -1;
    }

    if (filep->curNode == 0) {
        filep->curNode = filep->startNode;
        filep->offset = 0;
        filep->pos = 0;
        flashPageRead(&filep->hdr, filep->curNode, 0, sizeof(filep->hdr));
    }
    start = filep->pos;

    while (rsize > 0) {
        if (rsize > (filep->size - filep->offset)) {
            rsize = (filep->size - filep->offset);
        }
        if (rsize <= (FLASH_FILE_NODE_SIZE - filep->offset)) {
            flashPageRead(buffer, filep->curNode, filep->offset+sizeof(filep->hdr), rsize);
            filep->offset += rsize;
            filep->pos += rsize;
            if (filep->pos >= filep->size-1) {
                filep->eof = true;
            }
            return filep->pos - start;
        } else {
            dsize = FLASH_FILE_NODE_SIZE - filep->offset;
            flashPageRead(buffer, filep->curNode, filep->offset+sizeof(filep->hdr), dsize);
            filep->pos += dsize;
            rsize -= dsize;
            filep->curNode = filep->hdr.nextNode;
            filep->offset = 0;
            if (filep->curNode == 0) {
                filep->eof = true;
                return filep->pos - start;
            }

            // get next node header
            flashPageRead(&filep->hdr, filep->curNode, 0, sizeof(filep->hdr));
        }
    }
    return filep->pos - start;
}


/**
 * Marks a node as in use. Uses the flash chips internal buffer.
 */
void flashMapUseNode(uint16_t node)
{
    uint16_t mapPage = node / FLASH_NODES_PER_PAGE;
    uint16_t offset = (node % FLASH_NODES_PER_PAGE)/8;
    uint8_t map;

    flashBufLoad(mapPage);
    flashBufRead(&map, offset, sizeof(map));
    map &= ~(1 << (7-(node & 7)));
    flashBufSet(map, offset, 1);
    flashBufStore(mapPage);

    DPRINTF_P(PSTR("marked node %d as used (page %d, offset %d, map 0x%02x)\n"), node, mapPage, offset, map);
}


uint16_t flashNextFreeNode(void)
{
    uint8_t map[16]; // bitmap of pages free in this node map
    uint16_t node;
    uint16_t mapOffset = 0;

    // find a free node
    for (mapOffset=0; mapOffset < FLASH_MAP_SIZE; mapOffset += sizeof(map)) {
        flashRawRead(map, mapOffset, sizeof(map));
        for (uint8_t ind=0; ind<sizeof(map) && (mapOffset+ind) < FLASH_MAP_SIZE; ind++) {
            if (map[ind] != 0) {
                for (uint8_t bit=0; bit<8; bit++) {
                    if (map[ind] & (1<<(7-bit))) {
                        node = (mapOffset + ind) * 8 + bit;
                        DPRINTF_P(PSTR("Found free node %d at offset %d (map[%d]=0x%02x bit %d)\n"), node, mapOffset, ind, map[ind], bit);
                        return node;
                    }
                }
            }
        }
    }

    DPRINTF_P(PSTR("    No free nodes\n"));
    return 0;
}

/**
 * Allocate a node from the flash 
 * @returns the allocated node, or 0 if none available
 * @note - this overwrites the flashBuf
 */
uint16_t flashAllocNode(uint16_t node)
{

    if (node == 0) {
        node = flashNextFreeNode();
        if (node == 0) {
            return 0;   // no free nodes
        }
    }

    flashMapUseNode(node);

    return node;
}

/**
 * Create a file
 * @returns file id (page id of directory entry)
 * @note uses the internal buffer
 */
int flashCreate(char *filename, flashFile_t *filep)
{
    flashDirEntry_t dir;
    uint16_t lastDirPage = 0;
    uint16_t page = flashFindFile(filename, &dir, &lastDirPage);

    if (page != 0) {
        // file already exists. Overwrite?
        return -1;
    }

    if (lastDirPage == 0) {
        // no directory found, flash not formatted?
        return -3;
    }

    if (dir.nextEntryPage == 0xFFFF) {
        page = lastDirPage;
        lastDirPage = 0;
    } else {
        // create a directory entry
        page = flashAllocNode(0,0);
        if (page == 0) {
            // out of flash space
            return -2;
        }
        flashBufLoad(lastDirPage);
        dir.nextEntryPage = page;
        // XXX only need to mod lastEntryPage, not whole dir struct
        flashBufWrite(&dir, 0, sizeof(dir));
    }

    dir.size = 0;
    dir.startNode = 0;
    dir.endNode = 0;
    dir.prevEntryPage = lastDirPage;
    dir.nextEntryPage = 0;
    flashBufLoad(page);
    flashBufWrite(&dir, 0, sizeof(dir));
    flashBufWrite(filename, offsetof(flashDirEntry_t,name), strlen(filename)+1);
    flashBufStore(page);

    // return empty file ready for writing
    filep->startNode = 0;
    filep->endNode = 0;
    filep->size = 0;
    filep->offset = 0;
    filep->curNode = 0;
    filep->dirPage = page;
    filep->eof = true;
    return page;
}

/**
 * Write to file
 */
// XXX consider cache to internal buffer, flushing it when 
// full or if another operation will overwrite it.
int flashWrite(flashFile_t *filep, void *datap, size_t size)
{
    uint16_t node = filep->endNode;
    flashNode_t *nodep = (flashNode_t *)0;
    uint16_t offset;
    uint16_t space;

    if (node == 0) {
        // new file, allocate first node for it
        node = flashAllocNode(0,filep->fileId);
        if (node == 0) {
            return -2; // no room left
        }
        DPRINTF_P(PSTR("flashWrite(): new file, allocated node %d for it\n"), node);
        filep->offset = 0;
        space = FLASH_FILE_NODE_SIZE;
        offset = 0;
        filep->startNode = node;
        filep->endNode = node;
        filep->hdr.type = 0;
        filep->hdr.prevNode = 0;
        filep->hdr.nextNode = 0;
        flashBufWrite(&filep->hdr, 0, sizeof(filep->hdr));
        filep->curNode = node;
        flashBufSetCache(node);
    } else {
        DPRINTF_P(PSTR("flashWrite(): existing file, loading last node %d\n"), node);
        // file already exists, load the last node into the
        // internal buffer and read out the header
        flashBufLoad(node);
        if (filep->curNode != node) {
            flashPageErase(node);   // erase ready for write
            flashBufRead(&filep->hdr, 0, sizeof(filep->hdr));
            filep->curNode = node;
        }
        flashBufSetCache(node);

        offset = filep->size % FLASH_FILE_NODE_SIZE;
        if (offset) {
            space = FLASH_FILE_NODE_SIZE - offset;
        } else {
            // need to load the last page to store the new page number in the header
            space = 0;
        }
    }
    DPRINTF_P(PSTR("flashWrite(): node=%d size=%d space=%d\n"), node,size,space);

    while (size > space) {
        // fill up this node and allocate a new one
        uint16_t nextNode = flashNextFreeNode();
        if (nextNode == 0) {
            return -2; // no room left
        }
        DPRINTF_P(PSTR("flashWrite(): added new node %d\n"), nextNode);
        filep->hdr.nextNode = nextNode;
        filep->endNode = nextNode;
        DPRINTF_P(PSTR("flashWrite(): writing hdr\n"));
        flashBufWrite(&filep->hdr, 0, sizeof(filep->hdr));

        DPRINTF_P(PSTR("flashWrite(): writing %d bytes to node %d at offset %d hdr\n"), space, node, offset);
        flashBufWrite(datap, (uint16_t)&nodep->data[offset], space);
        flashFlushCache(node);

        flashAllocNode(nextNode, filep->fileId);  // this will flush the cache...

        // new header
        filep->hdr.prevNode = node;
        filep->hdr.nextNode = 0;
        node = nextNode;
        filep->curNode = node;
        flashBufSetCache(node);

        DPRINTF_P(PSTR("flashWrite(): writing hdr\n"));
        flashBufWrite(&filep->hdr, 0, sizeof(filep->hdr));

        filep->size += space;
        size -= space;

        space = FLASH_FILE_NODE_SIZE;
        offset = 0;
    }

    if (size) {
        DPRINTF_P(PSTR("flashWrite(): writing %d bytes to node %d at offset %d hdr\n"), size, node, offset);
        flashBufWrite(datap, (uint16_t)&nodep->data[offset], size);
        filep->size += size;
    }

    return 0;
}

int flashClose(flashFile_t *filep)
{
    // update file size in directory entry, and start/end nodes
    // XXX only do this on close to speed things up?
    flashDirEntry_t *dir = (flashDirEntry_t *)0;
    flashBufLoad(filep->dirPage);
    DPRINTF_P(PSTR("flashWrite(): updating dir entry %d, file size %d, endNode %d hdr\n"),
            filep->dirPage, filep->size, filep->endNode);
    flashBufWrite(&filep->size, (uint16_t)&dir->size, sizeof(dir->size));
    flashBufWrite(&filep->startNode, (uint16_t)&dir->startNode, sizeof(dir->startNode));
    flashBufWrite(&filep->endNode, (uint16_t)&dir->endNode, sizeof(dir->endNode));
    flashBufEraseStore(filep->dirPage);

    return 0;
}
