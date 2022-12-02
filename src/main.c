
#include <stdio.h>
#include <string.h>

#include "disk.h"
#include "utils.h"

static
void usage(const char *prog_name)
{
    printf("Usage:\n");
    printf(" %s [options] disk\n", prog_name);
    printf("where:\n");
    printf("  --help        Print this help\n");
}

int main(int argc, char **argv)
{

    const char *disk_filename;
    unsigned int num_cylinders;
    unsigned int num_heads;
    unsigned int num_sectors;
    struct disk d;
    int i;

    disk_filename = NULL;
    num_cylinders = 203;
    num_heads = 2;
    num_sectors = 12;

    for (i = 1; i < argc; i++) {
        if (strcmp("--help", argv[i]) == 0
                   || strcmp("-h", argv[i]) == 0) {
            usage(argv[0]);
            return 0;
        } else {
            disk_filename = argv[i];
        }
    }

    if (!disk_filename) {
        report_error("main: must specify the disk file name");
        return 1;
    }

    disk_initvar(&d);

    if (unlikely(!disk_create(&d, num_cylinders, num_heads, num_sectors))) {
        report_error("main: could not create disk");
        goto error;
    }

    if (unlikely(!disk_load_image(&d, disk_filename))) {
        report_error("main: could not load disk image");
        goto error;
    }

    if (unlikely(!disk_check_integrity(&d))) {
        report_error("main: invalid disk");
        goto error;
    }

    disk_print_summary(&d);

    disk_destroy(&d);
    return 0;

error:
    disk_destroy(&d);
    return 1;
}
