
#ifndef __DISK_H
#define __DISK_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Data structures and types. */

/* Structure representing a sector. */
struct sector {
    uint16_t header[2];           /* Sector header. */
    struct {
        uint16_t next_rda;        /* The (real) address of next sector. */
        uint16_t prev_rda;        /* The (real) addres of previous sector. */
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

/* Writes the contents of the disk to a file named `filename`.
 * Returns TRUE on success.
 */
int disk_save_image(const struct disk *d, const char *filename);

/* Checks the integrity of the disk.
 * Returns TRUE on success.
 */
int disk_check_integrity(const struct disk *d);

/* Finds a file in the disk filesystem.
 * The name of the file to find is given in `name`.
 * The parameter `leader_vda` will contain the virtual address
 * of the leader sector of the file.
 * Returns TRUE on success.
 */
int disk_find_file(const struct disk *d, const char *name,
                   uint16_t *leader_vda);

/* Extracts a file from the disk.
 * The `leader_vda` is the virtual address of the leader sector
 * of the file in the disk. The `filename` specifies the filename
 * to write on the user (native) filesystem. Lastly if `include_leader`
 * is set to TRUE, this function will also dump the contents of the
 * leader page.
 * Returns TRUE on success.
 */
int disk_extract_file(const struct disk *d, uint16_t leader_vda,
                      const char *filename, int include_leader);

/* Determines a file length.
 * The virtual address of the leader sector of the file is in `leader_vda`.
 * The file length is returned in `length`.
 * Returns TRUE on success.
 */
int disk_file_length(const struct disk *d, uint16_t leader_vda,
                     uint16_t *length);

/* Obtains the file times.
 * The virtual address of the laeader sector of the file is in `leader_vda`.
 * The creation, last written, and access times are returned in
 * `created`, `written`, `read`, respectively.
 * Returns TRUE on success.
 */
int disk_file_times(const struct disk *d, uint16_t leader_vda,
                    time_t *created, time_t *written, time_t *read);

/* Prints a summary of the disk.
 * Returns TRUE on success.
 */
int disk_print_summary(const struct disk *d);

/* Converts a real address to a virtual address.
 * The real address is in `rda` and the virtual address is returned
 * in the `vda` parameter.
 * Returns TRUE on success.
 */
int disk_real_to_virtual(const struct disk *d, uint16_t rda, uint16_t *vda);

/* Converts a virtual address to a real address.
 * The virtual address is in `vda` and the real address is returned
 * in the `rda` parameter.
 * Returns TRUE on success.
 */
int disk_virtual_to_real(const struct disk *d, uint16_t vda, uint16_t *rda);


#endif /* __DISK_H */
