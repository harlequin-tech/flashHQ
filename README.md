A library for the <a href="http://www.adestotech.com/products/dataflash">adesto AT45DB family</a> of SPI serial dataflash chips.

This library provides low level functions to read data, erase data, and write data to the serial FLASH chips.

It also provides a basic flash filesystem designed to minimise the RAM needed for file operations while ensuring good performance.

Examples
--------

Example code to format the flash chip and create a file.

    #include "flashfile.h"

    flashFile_t file;
    flashInit();    // Initialize flash memory
    flashFormat();

    int res = flashCreate("test.txt", &file);
    if (res < 0) {
        printf("Failed to create test.txt, res=%d\n", res);
	while (1);
    }

    res = flashOpen("test.txt", &file);
    if (res < 0) {
        printf("Failed to open test.txt, res=%d\n", res);
        while (1);
    }
    for (uint16_t fill = 0; fill < 4096; fill++) {
        flashWrite(&file, &fill, sizeof(fill));
    }
    flashClose(&file);


Notes
-----

1. The flashfile system is in an alpha state, still being tested and developed.
