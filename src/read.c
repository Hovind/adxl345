#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#define PATH "/dev/adxl345"

int main() {
    int fd;
    char byte;
    ssize_t size;

    fd = open(PATH, O_RDONLY);
    if (fd == -1) {
        printf("Failed to open %s\n", PATH);
        return fd;
    }

    size = read(fd, &byte, 1);
    printf("Read byte %d of length %zd\n", byte, size);
    return 0;
}
