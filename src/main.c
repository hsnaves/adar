
#include <stdio.h>
#include <string.h>

#include "fs.h"
#include "utils.h"

/* Callback to print the files in the filesystem. */
static
int print_files_cb(const struct fs *fs,
                   const struct file_entry *fe,
                   void *arg)
{
    struct file_info finfo;
    size_t length;

    if (unlikely(!fs_file_info(fs, fe, &finfo))) {
        report_error("main: could not get file information");
        return -1;
    }

    if (unlikely(!fs_file_length(fs, fe, &length))) {
        report_error("main: could not get file length");
        return -1;
    }

    printf("%-6o %-6o   %-6o  %-38s\n",
           fe->leader_vda, fe->sn.file_id, (unsigned int) length,
           finfo.filename);

    return 1;
}

/* Main function to print the files in the filesystem.
 * Returns TRUE on success.
 */
static
int print_files(const struct fs *fs)
{
    printf("VDA    FILE_ID  SIZE    FILENAME\n");
    if (unlikely(!fs_scan_files(fs, &print_files_cb, NULL))) {
        report_error("main: could not print files");
        return FALSE;
    }
    return TRUE;
}

/* Callback to print the files in the directory. */
static
int print_directory_cb(const struct fs *fs,
                       const struct directory_entry *de,
                       void *arg)
{
    printf("%-6o %-6o   %-6o     %-38s\n",
           de->fe.leader_vda, de->fe.sn.file_id,
           de->fe.version, de->filename);

    return 1;
}

/* Main function to print the files in the directory pointed by `fe`.
 * Returns TRUE on success.
 */
static
int print_directory(const struct fs *fs, const struct file_entry *fe)
{
    printf("VDA    FILE_ID  VERSION    FILENAME\n");
    if (unlikely(!fs_scan_directory(fs, fe, &print_directory_cb, NULL))) {
        report_error("main: could not print directory");
        return FALSE;
    }
    return TRUE;
}


/* Prints the usage information to the console output. */
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
    struct geometry dg;
    struct fs fs;
    struct file_entry fe;
    int i, is_last, print_summary;

    disk_filename = NULL;
    extract_filename = NULL;
    print_summary = FALSE;

    dg.num_cylinders = 203;
    dg.num_heads = 2;
    dg.num_sectors = 12;

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

    fs_initvar(&fs);

    if (unlikely(!fs_create(&fs, dg))) {
        report_error("main: could not create disk");
        goto error;
    }

    if (unlikely(!fs_load_image(&fs, disk_filename))) {
        report_error("main: could not load disk image");
        goto error;
    }

    if (unlikely(!fs_check_integrity(&fs))) {
        report_error("main: invalid disk");
        goto error;
    }

    if (extract_filename != NULL) {
        if (unlikely(!fs_find_file(&fs, extract_filename, &fe))) {
            report_error("main: could not find %s", extract_filename);
            goto error;
        }

        if (unlikely(!fs_extract_file(&fs, &fe, extract_filename))) {
            report_error("main: could not extract %s", extract_filename);
            goto error;
        }

        printf("extracted `%s` successfully\n", extract_filename);
    }

    if (print_summary) {
        if (unlikely(!print_files(&fs))) goto error;
        printf("\n\n");

        if (unlikely(!fs_file_entry(&fs, 1, &fe))) {
            report_error("main: could not find main directory");
        }

        if (unlikely(!print_directory(&fs, &fe))) goto error;
    }


    fs_destroy(&fs);
    return 0;

error:
    fs_destroy(&fs);
    return 1;
}
