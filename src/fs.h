
#ifndef __FS_H
#define __FS_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Constants. */
#define FILENAME_LENGTH                              40U
#define PAGE_DATA_SIZE                              512U

/* To interpret the bits of serial_number.word1. */
#define SN_DIRECTORY                             0x8000U
#define SN_RAND                                  0x4000U
#define SN_NOLOG                                 0x2000U
#define SN_PART1_MASK                            0x1FFFU

#define VERSION_FREE                             0xFFFFU
#define VERSION_BAD                              0xFFFEU

/* Data structures and types. */

/* The serial number of a file.
 * Corresponds to the SN structure in AltoFileSys.D.
 */
struct serial_number {
    uint16_t word1;               /* Some bits are interesting here:
                                   * 0x8000: indicates it is a directory
                                   * 0x4000: random bit (not used)
                                   * 0x2000: no longer used
                                   * 0x1FFF: part 1
                                   */
    uint16_t word2;               /* Second word of serial number
                                   * (part2).
                                   */
};

/* Describes a particular file by label & leader page disk address.
 * Corresponds to the FP structure in AltoFileSys.D.
 */
struct file_entry {
    struct serial_number sn;      /* The serial number of the file. */
    uint16_t version;             /* The file version. */
    uint16_t blank;               /* Expansion to 2-word vda. */
    uint16_t leader_vda;          /* The vda (virtual disk address)
                                   * of the leader page of the file.
                                   */
};

/* Structure to represent a position within an open file.
 * Corresponds to the FA structure in AltoFileSys.D.
 */
struct file_position {
    uint16_t vda;                 /* The virtual disk address of the
                                   * current page.
                                   */
    uint16_t pgnum;               /* The index of the page within the file.
                                   * Leader page has index 0.
                                   */
    uint16_t pos;                 /* The position with respect to the
                                   * current page.
                                   */
};

/* Structure to represent an open file.
 * Roughly corresponds to the CFA structure in AltoFileSys.D.
 */
struct open_file {
    struct file_entry fe;         /* The file_entry information. */
    struct file_position pos;     /* The file_position information. */
    int error;                    /* Indicates the file has error. */
};

/* Structure representing a filesystem page (sector). */
struct page {
    uint16_t page_vda;            /* The virtual disk address of the page. */
    uint16_t header[2];           /* Page header. */
    struct {
        uint16_t next_rda;        /* The (real) DA of next page. */
        uint16_t prev_rda;        /* The (real) DA of previous page. */
        uint16_t unused;
        uint16_t nbytes;          /* Number of used bytes in the page. */
        uint16_t file_pgnum;      /* Page number of a file. */
        uint16_t version;         /* Notable values:
                                   * 0xFFFF for free pages.
                                   * 0xFFF2 for bad pages.
                                   */
        struct serial_number sn;  /* The file serial number. */
    } label;
    uint8_t data[512];            /* Page data. */
};

/* Structure to represent an entry within a directory.
 * Corresponds to the DV structure in AltoFileSys.D.
 */
struct directory_entry {
    char filename[FILENAME_LENGTH]; /* The name of the file. */
    struct file_entry fe;           /* A pointer to file_entry information. */
};

/* The file information (from the leader page).
 * Roughly corresponds to the LD structure in AltoFileSys.D.
 */
struct file_info {
    char filename[FILENAME_LENGTH]; /* The name of the file (hint). */
    time_t created;                 /* The time the file was created. */
    time_t written;                 /* The time the file was written. */
    time_t read;                    /* The time the file was accessed. */

    uint8_t consecutive;            /* The consecutive value. */
    uint8_t change_sn;              /* The change serial number value. */

    struct file_entry dir_fe;       /* Hint to the directory entry. */
    struct file_position last_page; /* Hint to the last page. */
};

/* Structure representing the disk geometry. */
struct geometry {
    uint16_t num_cylinders;       /* Number of cylinders. */
    uint16_t num_heads;           /* Number of heads per cylinder. */
    uint16_t num_sectors;         /* Number of sectors per head. */
};

/* Structure representing the filesystem. */
struct fs {
    struct geometry dg;           /* The disk geometry. */
    struct page *pages;           /* Filesystem pages (sectors). */
    uint16_t length;              /* Total length of the filesystem
                                   * in pages.
                                   */
};

/* Defines the type of the callback function for fs_scan_files().
 * The callback should return a positive number to continue scanning,
 * zero to stop scanning, and a negative number on error.
 */
typedef int (*scan_files_cb)(const struct fs *fs,
                             const struct file_entry *fe,
                             void *arg);

/* Defines the type of the callback function for fs_scan_directory().
 * The callback should return a positive number to continue scanning,
 * zero to stop scanning, and a negative number on error.
 */
typedef int (*scan_directory_cb)(const struct fs *fs,
                                 const struct directory_entry *de,
                                 void *arg);

/* Functions. */

/* Initializes the fs variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void fs_initvar(struct fs *fs);

/* Destroys the fs object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void fs_destroy(struct fs *fs);

/* Creates a new fs object.
 * This obeys the initvar / destroy / create protocol.
 * The parameter `dg` specifies the disk geometry.
 * Returns TRUE on success.
 */
int fs_create(struct fs *fs, struct geometry dg);

/* Reads the contents of the disk from a file named `filename`.
 * Returns TRUE on success.
 */
int fs_load_image(struct fs *fs, const char *filename);

/* Writes the contents of the disk to a file named `filename`.
 * Returns TRUE on success.
 */
int fs_save_image(const struct fs *fs, const char *filename);

/* Checks the integrity of the filesystem.
 * Returns TRUE on success.
 */
int fs_check_integrity(const struct fs *fs);

/* Opens a file for reading or writing.
 * The file is specified by `fe` and the open file is stored in `of`.
 * If `include_leader` is TRUE, it starts reading or writing from
 * the leader page.
 * Returns TRUE on success.
 */
int fs_open(const struct fs *fs, const struct file_entry *fe,
            struct open_file *of, int include_leader);

/* Reads `len` bytes of an open file `of` to `dst`.
 * If `dst` is NULL, the file pointer in `of` is still updated,
 * but no actual bytes are copied.
 * Returns the number of bytes read.
 */
size_t fs_read(const struct fs *fs, struct open_file *of,
               uint8_t *dst, size_t len);

/* Writes `len` bytes of an open file `of` from `src`.  If `src` is
 * NULL, the file is zeroed. The parameter `extends` tells the function
 * to allocate free pages when it reaches the end of the file,
 * thereby extending the existing file.
 * Returns the number of written bytes.
 */
size_t fs_write(struct fs *fs, struct open_file *of,
                const uint8_t *src, size_t len, int extend);

/* Trims the file to have the size matching the current position
 * in the file.
 * Returns TRUE on success.
 */
int fs_trim(struct fs *fs, struct open_file *of);

/* Extracts a file from the filesystem.
 * The `fe` contains information about the location of the file in
 * in the filesystem. The `output_filename` specifies the filename
 * of the output file to write.
 * Returns TRUE on success.
 */
int fs_extract_file(const struct fs *fs, const struct file_entry *fe,
                    const char *output_filename);

/* Replaces a file from the filesystem.
 * Note: the file must currently exist in the filesystem!
 * The `fe` contains information about the location of the file
 * to be replaced. The `input_filename` specifies the filename
 * of the input file to read from.
 * Returns TRUE on success.
 */
int fs_replace_file(struct fs *fs, const struct file_entry *fe,
                    const char *input_filename);

/* Converts the virtual disk address of the leader page `leader_vda` of a
 * file to a file_entry object `fe`.
 * Returns TRUE on success.
 */
int fs_file_entry(const struct fs *fs, uint16_t leader_vda,
                  struct file_entry *fe);

/* Determines a file length.
 * The `fe` determines the file.
 * The file length is returned in `length`.
 * Returns TRUE on success.
 */
int fs_file_length(const struct fs *fs, const struct file_entry *fe,
                   size_t *length);

/* Obtains the file metadata at the leader page.
 * This includes the name of the file, access and modification times,
 * etc.
 * Returns TRUE on success.
 */
int fs_file_info(const struct fs *fs, const struct file_entry *fe,
                 struct file_info *finfo);

/* Finds a file in the filesystem.
 * The name of the file to find is given in `filename`.
 * The parameter `fe` will be populated with information about
 * the scavenged file (such as leader page virtual disk address, etc.).
 * Returns TRUE on success.
 */
int fs_find_file(const struct fs *fs, const char *filename,
                 struct file_entry *fe);

/* Scavenges  a file in the filesystem.
 * This is different from fs_find_file() as it does not use any information
 * about the SysDir directory, it is solely based on the leader
 * pages of files (and so relies completely on hints).
 * The name of the file to find is given in `filename`.
 * The parameter `fe` will be populated with information about
 * the scavenged file (such as leader page virtual disk address, etc.).
 * Returns TRUE on success.
 */
int fs_scavenge_file(const struct fs *fs, const char *filename,
                     struct file_entry *fe);

/* Scans the files in the filesystem.
 * The callback `cb` is used to scan the filesystem. The `arg` is
 * an extra parameter passed to the callback.
 * Returns TRUE on success.
 */
int fs_scan_files(const struct fs *fs, scan_files_cb cb, void *arg);

/* Scans one directory..
 * The directory is specified by the file_entry `fe` parameter.
 * The callback `cb` is used to scan the directory. The `arg` is
 * an extra parameter passed to the callback.
 * Returns TRUE on success.
 */
int fs_scan_directory(const struct fs *fs, const struct file_entry *fe,
                      scan_directory_cb cb, void *arg);


#endif /* __FS_H */
