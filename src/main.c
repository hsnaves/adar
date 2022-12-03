
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "fs.h"
#include "utils.h"


/* Prints the details of a file_info structure. */
static
void print_file_info_details(struct file_info *finfo)
{
    struct tm *ltm;

    ltm = localtime(&finfo->created);
    printf("Created: %02d-%02d-%02d %2d:%02d:%02d\n",
           ltm->tm_mday, ltm->tm_mon + 1,
           ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
    ltm = localtime(&finfo->written);
    printf("Written: %02d-%02d-%02d %2d:%02d:%02d\n",
           ltm->tm_mday, ltm->tm_mon + 1,
           ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
    ltm = localtime(&finfo->read);
    printf("Read:    %02d-%02d-%02d %2d:%02d:%02d\n",
           ltm->tm_mday, ltm->tm_mon + 1,
           ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec);
    printf("Consecutive: %u\n", finfo->consecutive);
    printf("Change SN: %u\n", finfo->change_sn);
    printf("Directory: \n");
    printf("  VDA: %u\n", finfo->dir_fe.leader_vda);
    printf("  SN: %u\n", finfo->dir_fe.sn.word2);
    printf("  VER: %u\n", finfo->dir_fe.version);
    printf("Last page: \n");
    printf("  VDA: %u\n", finfo->last_page.vda);
    printf("  PGNUM: %u\n", finfo->last_page.pgnum);
    printf("  POS: %u\n", finfo->last_page.pos);
}

/* Callback to print the files in the filesystem. */
static
int print_files_cb(const struct fs *fs,
                   const struct file_entry *fe,
                   void *arg)
{
    struct file_info finfo;
    size_t length;
    int verbose;

    verbose = *((int *) arg);
    if (unlikely(!fs_file_info(fs, fe, &finfo))) {
        report_error("main: could not get file information");
        return -1;
    }

    if (unlikely(!fs_file_length(fs, fe, &length))) {
        report_error("main: could not get file length");
        return -1;
    }

    if (verbose) {
        printf("Leader VDA: %u\n", fe->leader_vda);
        printf("Serial number: %u\n", fe->sn.word2);
        printf("Version: %u\n", fe->version);
        printf("Name: %s\n", finfo.filename);
        printf("Length: %u\n", (unsigned int) length);
        if (verbose > 1) {
            print_file_info_details(&finfo);
        }
        printf("\n");
    } else {
        printf("%-6u %-6u %-6u %-6u  %-38s\n",
               fe->leader_vda, fe->sn.word2, fe->version,
               (unsigned int) length, finfo.filename);
    }

    return 1;
}

/* Main function to print the files in the filesystem.
 * The verbosity level is indicated by `verbose`.
 * Returns TRUE on success.
 */
static
int print_files(const struct fs *fs, int verbose)
{
    if (!verbose)
        printf("VDA    SN     VER    SIZE    FILENAME\n");

    if (unlikely(!fs_scan_files(fs, &print_files_cb, &verbose))) {
        report_error("main: could not print files");
        return FALSE;
    }

    return TRUE;
}

/* Callback to print the files in the directory. */
static
int print_dir_cb(const struct fs *fs,
                 const struct directory_entry *de,
                 void *arg)
{
    struct file_info finfo;
    size_t length;
    int verbose;

    verbose = *((int *) arg);

    if (unlikely(!fs_file_info(fs, &de->fe, &finfo))) {
        report_error("main: could not get file information");
        return -1;
    }

    if (unlikely(!fs_file_length(fs, &de->fe, &length))) {
        report_error("main: could not get file length");
        return -1;
    }

    if (verbose) {
        printf("Leader VDA: %u\n", de->fe.leader_vda);
        printf("Serial number: %u\n", de->fe.sn.word2);
        printf("Version: %u\n", de->fe.version);
        printf("Name: %s\n", de->filename);
        printf("Length: %u\n", (unsigned int) length);
        if (verbose > 1) {
            print_file_info_details(&finfo);
        }
        printf("\n");
    } else {
        printf("%-6u %-6u %-6u %-6u  %-38s\n",
               de->fe.leader_vda, de->fe.sn.word2, de->fe.version,
               (unsigned int) length, de->filename);
    }

    return 1;
}

/* Main function to print the files in the directory pointed by `fe`.
 * The verbosity level is indicated by `verbose`.
 * Returns TRUE on success.
 */
static
int print_directory(const struct fs *fs,
                    const struct file_entry *fe,
                    int verbose)
{
    if (!verbose)
        printf("VDA    SN     VER    SIZE    FILENAME\n");

    if (unlikely(!fs_scan_directory(fs, fe, &print_dir_cb, &verbose))) {
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
    printf("  -l            Lists all files in the filesystem\n");
    printf("  -d dirname    Lists the contents of a directory\n");
    printf("  -e filename   Extracts a given file\n");
    printf("  -s            Scavenges files instead of finding them\n");
    printf("  -v            Increase verbosity\n");
    printf("  --help        Print this help\n");
}

int main(int argc, char **argv)
{

    const char *disk_filename;
    const char *extract_filename;
    const char *dirname;
    struct geometry dg;
    struct fs fs;
    struct file_entry fe;
    int list_files, do_scavenge;
    int i, is_last;
    int verbose;

    disk_filename = NULL;
    extract_filename = NULL;
    dirname = NULL;
    list_files = FALSE;
    do_scavenge = FALSE;
    verbose = 0;

    dg.num_cylinders = 203;
    dg.num_heads = 2;
    dg.num_sectors = 12;

    for (i = 1; i < argc; i++) {
        is_last = (i + 1 == argc);
        if (strcmp("-l", argv[i]) == 0) {
            list_files = TRUE;
        } else if (strcmp("-d", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the directory to list");
                return 1;
            }
            dirname = argv[++i];
        } else if (strcmp("-e", argv[i]) == 0) {
            if (is_last) {
                report_error("main: please specify the file to extract");
                return 1;
            }
            extract_filename = argv[++i];
        } else if (strcmp("-s", argv[i]) == 0) {
            do_scavenge = TRUE;
        } else if (strcmp("-v", argv[i]) == 0) {
            verbose++;
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
        if (do_scavenge) {
            if (unlikely(!fs_scavenge_file(&fs, extract_filename, &fe))) {
                report_error("main: could not scavenge %s", extract_filename);
                goto error;
            }
        } else {
            if (unlikely(!fs_find_file(&fs, extract_filename, &fe))) {
                report_error("main: could not find %s", extract_filename);
                goto error;
            }
        }

        if (unlikely(!fs_extract_file(&fs, &fe, extract_filename))) {
            report_error("main: could not extract %s", extract_filename);
            goto error;
        }

        printf("extracted `%s` successfully\n", extract_filename);
    }

    if (list_files) {
        if (unlikely(!print_files(&fs, verbose))) goto error;
    }

    if (dirname) {
        if (do_scavenge) {
            if (unlikely(!fs_scavenge_file(&fs, dirname, &fe))) {
                report_error("main: could not scavenge %s", dirname);
                goto error;
            }
        } else {
            if (unlikely(!fs_find_file(&fs, dirname, &fe))) {
                report_error("main: could not find %s", dirname);
                goto error;
            }
        }

        if (unlikely(!(fe.sn.word1 & SN_DIRECTORY))) {
            report_error("main: %s is not a directory", dirname);
            goto error;
        }

        if (unlikely(!print_directory(&fs, &fe, verbose))) goto error;
    }


    fs_destroy(&fs);
    return 0;

error:
    fs_destroy(&fs);
    return 1;
}
