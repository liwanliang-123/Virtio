#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    int fd, retvalue;
    unsigned char databuf[1];  // 0 - 255

    if(argc != 2) {
        printf("ERROR:please enter two parameters!\n");
        printf("eg: ./virtio number\n");
        printf("but the number between 0 and 255!\n");
        return -1;
    }

    databuf[0] = atoi(argv[1]); /* string to number */

    fd = open("/dev/virtio_misc", O_RDWR);
    if(fd < 0) {
        printf("ERROR:virtio_misc open failed!\n");
        return -1;
    }

    retvalue = write(fd, databuf, sizeof(databuf));
    if(retvalue < 0) {
        printf("ERROR:write failed!\r\n");
        close(fd);
        return -1;
    }

    close(fd);

    return 0;
}
