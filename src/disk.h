
#ifndef __DISK_H
#define __DISK_H

#include <stddef.h>
#include <stdint.h>

/* Data structures and types. */

/* Structure representing a sector. */
struct sector {
    uint16_t header[2];           /* Sector header. */
    struct {
        uint16_t da_next;         /* The address of next sector. */
        uint16_t da_prev;         /* The addres of previous sector. */
        uint16_t unused;
        uint16_t nbytes;          /* Number of bytes. */
        uint16_t file_secnum;     /* Sector number of a file */
        uint16_t fid[3];          /* File identification. */

        /* fid[0] is 1 for used files, 0xffff for free sectors. */
        /* fid[1] is 0x8000 for a directory, 0 for regular, 0xffff for free. */
        /* fid[2] is the file_id. */

    } label;
    uint8_t data[512];            /* Sector data. */
};

/* Structure representing a disk. */
struct disk {
    unsigned int num_cylinders;   /* Number of cylinders (disk geometry). */
    unsigned int num_heads;       /* Number of heads per cylinder. */
    unsigned int num_sectors;     /* Number of sectors per head. */

    struct sector *sectors;       /* Disk sectors */
    unsigned int length;          /* Total length of the disk in sectors. */
};

/* Functions. */

/* Initializes the disk variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void disk_initvar(struct disk *d);

/* Destroys the disk object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void disk_destroy(struct disk *d);

/* Creates a new disk object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int disk_create(struct disk *d, unsigned int num_cylinders,
                unsigned int num_heads, unsigned int num_sectors);

/* Reads the contents of the disk from a file named `filename`.
 * Returns TRUE on success.
 */
int disk_read(struct disk *d, const char *filename);

/* Prints a summary of the disk. */
void disk_print_summary(struct disk *d);


#endif /* __DISK_H */
