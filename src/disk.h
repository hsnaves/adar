
#ifndef __DISK_H
#define __DISK_H

#include <stddef.h>
#include <stdint.h>

/* Data structures and types. */

/* Structure representing a sector. */
struct sector {
    uint16_t header[2];           /* Sector header. */
    struct {
        uint16_t rda_next;        /* The (real) address of next sector. */
        uint16_t rda_prev;        /* The (real) addres of previous sector. */
        uint16_t unused;
        uint16_t nbytes;          /* Number of used bytes in the sector. */
        uint16_t file_secnum;     /* Sector number of a file. */
        uint16_t fid[3];          /* File identification. */

        /* fid[0] is 1 for used files, 0xffff for free sectors. */
        /* fid[1] is 0x8000 for a directory, 0 for regular, 0xffff for free. */
        /* fid[2] is the file_id. */

    } label;
    uint8_t data[512];            /* Sector data. */
};

/* Structure representing a disk. */
struct disk {
    uint16_t num_cylinders;       /* Number of cylinders (disk geometry). */
    uint16_t num_heads;           /* Number of heads per cylinder. */
    uint16_t num_sectors;         /* Number of sectors per head. */

    struct sector *sectors;       /* Disk sectors. */
    uint16_t length;              /* Total length of the disk in sectors. */
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
int disk_create(struct disk *d, uint16_t num_cylinders,
                uint16_t num_heads, uint16_t num_sectors);

/* Reads the contents of the disk from a file named `filename`.
 * Returns TRUE on success.
 */
int disk_load_image(struct disk *d, const char *filename);

/* Converts a real address to a virtual address.
 * The real address is in `rda` and the virtual address is returned
 * in the `vda` parameter.
 * Returns TRUE on success.
 */
int disk_real_to_virtual(struct disk *d, uint16_t rda, uint16_t *vda);

/* Converts a virtual address to a real address.
 * The virtual address is in `vda` and the real address is returned
 * in the `rda` parameter.
 * Returns TRUE on success.
 */
int disk_virtual_to_real(struct disk *d, uint16_t vda, uint16_t *rda);

/* Determines a file length.
 * The virtual address of the first sector of the file is in `first_vda`.
 * The file length is returned in `length`.
 * Returns TRUE on success.
 */
int disk_file_length(struct disk *d, unsigned int first_vda, uint16_t *length);

/* Prints a summary of the disk.
 * Returns TRUE on success.
 */
int disk_print_summary(struct disk *d);


#endif /* __DISK_H */
