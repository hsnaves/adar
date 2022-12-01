
#ifndef __DISK_H
#define __DISK_H

#include <stddef.h>
#include <stdint.h>

/* Data structures and types. */

/* Structure representing a sector. */
struct sector {
    uint16_t header[2];           /* Sector header. */
    uint16_t label[8];            /* Sector label. */
    uint16_t data[256];           /* Sector data. */
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


#endif /* __DISK_H */
