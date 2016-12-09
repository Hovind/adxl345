#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <adxl345.h>

#define PATH "/dev/adxl345"
int main() {
    int fd;
    signed char data;
    fd_set afds, rfds;
    struct timeval tv;
    ssize_t size;
    char c;
    unsigned int axis;
    
    /* Open device */
    fd = open(PATH, O_RDONLY);
    if (fd < 0) {
        printf("Failed to open %s\n", PATH);
        return fd;
    }

    /* Initialize the set of active sockets. */
    FD_ZERO(&afds);
    /* FD_SET(fd, &afds); */
    FD_SET(0, &afds);

    /* Initialize timeval */
    tv.tv_sec = 0;
    tv.tv_usec = 1;

    for (;;) {
        /* Block until input arrives on one or more active sockets. */
        rfds = afds;
        if (select (1, &rfds, NULL, NULL, &tv) < 0) {
            printf("Select failed\n");
            return 1;
        }

        /* if (FD_ISSET(fd, &rfds)) {} */
            
        if (FD_ISSET(0, &rfds)) {
            c = getchar();
            /* Check for exit condition */
            if (c == 'q') {
                printf("Received \'q\', exiting ...\n");
                return 0;
            }

            /* Get axis */
            axis = c - '0';

            /* Check axis validity */
            if (axis < 3) { 
                /* Set axis */
                ioctl(fd, ADXL345_SET_AXIS_X + axis);
                printf("Setting axis to %u\n", axis);
            }
        }
        size = read(fd, &data, 1);
        printf("d: %i, l: %zd\n", data, size);


    }

    return 0;
}
