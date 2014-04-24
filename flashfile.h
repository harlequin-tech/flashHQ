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

#ifndef _FLASHFILE_H
#define _FLASHFILE_H

#include <stdbool.h>
#include "flashHQ.h"

#define FLASH_AVAILBLE_SIZE ((FLASH_NUM_PAGES + 7)/8);
#define FLASH_MAP_SIZE  (FLASH_NUM_PAGES/8)		// number of bytes needed for node map

#define FLASH_FILE_NODE_SIZE (FLASH_PAGE_SIZE - sizeof(flashNodeHeader_t))
#define FLASH_NODE_SIZE FLASH_PAGE_SIZE
#define FLASH_FREE_SIZE_264   16   // bitmap size per map
#define FLASH_FREE_NODES_264  124  // nodes per map
#define FLASH_FREE_SIZE_528   32   // bitmap size per map
#define FLASH_FREE_NODES_528  248  // nodes per map

#define FLASH_FREE_NODES FLASH_FREE_NODES_264
#define FLASH_NODES_PER_PAGE (FLASH_PAGE_SIZE*8)
#define FLASH_MAP_PAGE_COUNT  (FLASH_NUM_PAGES / (2*FLASH_NODES_PER_PAGE-1) + 1)
#define FLASH_DIR_START_PAGE  FLASH_MAP_PAGE_COUNT

// Node header - a doubly-linked list of nodes
typedef struct {
    uint8_t type;
    uint16_t prevNode;	
    uint16_t nextNode;	
} flashNodeHeader_t;

// file data node
// Files consist of chains of one or more nodes holding the file contents
typedef struct {
    flashNodeHeader_t hdr;
    uint8_t data[];
} flashNode_t;


#define FLASH_DIR_HEADER_SIZE	14
typedef struct {
    uint32_t size;		// size in bytes
    uint16_t startNode;
    uint16_t endNode;
    uint16_t nextEntryPage;
    uint16_t prevEntryPage;
    char name[];
} flashDirEntry_t;

typedef struct {
    uint32_t size;	//*< size in bytes
    uint16_t startNode;	//*< page of start node
    uint16_t endNode;	//*< page of end node
    uint16_t offset;	//*< offset in current node
    uint16_t pos;	//*< read position in file
    uint16_t dirPage;	//*< Directory page
    bool eof;
    uint16_t curNode;	//*< current node
    flashNodeHeader_t hdr;
} flashFile_t;

void flashFormat(void);
uint16_t flashAllocNode(uint16_t node);
int flashOpen(char *filename, flashFile_t *filep);
int flashRead(flashFile_t *filep, uint8_t *buffer, uint16_t size);
int flashSeek(flashFile_t *filep, uint16_t filepos);
int flashClose(flashFile_t *filep);
int flashCreate(char *filename, flashFile_t *filep);
int flashWrite(flashFile_t *filep, void *datap, size_t size);

#endif
