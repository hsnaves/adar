
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
    printf("  -s            Prints a summary of the filesystem\n");
    printf("  -e filename   Extracts a given file\n");
    printf("  --help        Print this help\n");
}

int main(int argc, char **argv)
{

    const char *disk_filename;
    const char *extract_filename;
    unsigned int num_cylinders;
    unsigned int num_heads;
    unsigned int num_sectors;
    struct disk d;
    uint16_t leader_vda;
    int i, is_last, print_summary;

    disk_filename = NULL;
    extract_filename = NULL;
    print_summary = FALSE;

    num_cylinders = 203;
    num_heads = 2;
    num_sectors = 12;

    for (i = 1; i < argc; i++) {
        is_last = (i + 1 == argc);
        if (strcmp("-e", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the file to extract");
                return 1;
            }
            extract_filename = argv[++i];
        } else if (strcmp("-s", argv[i]) == 0) {
            print_summary = TRUE;
        } else if (strcmp("--help", argv[i]) == 0
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

    if (extract_filename != NULL) {
        if (unlikely(!disk_find_file(&d, extract_filename, &leader_vda))) {
            report_error("main: could not find %s", extract_filename);
            goto error;
        }

        if (unlikely(!disk_extract_file(&d, leader_vda,
                                        extract_filename, FALSE))) {
            report_error("main: could not extract %s", extract_filename);
            goto error;
        }

        printf("extracted `%s` successfully\n", extract_filename);
    }

    if (print_summary) {
        if (unlikely(!disk_print_summary(&d))) {
            report_error("main: could not print summary");
            goto error;
        }
    }

    disk_destroy(&d);
    return 0;

error:
    disk_destroy(&d);
    return 1;
}
