
//Example code to format the flash chip and create a file.

#include "flashHQ.h"
#include "flashfile.h"

void setup()
{
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
}

void loop()
{
}
