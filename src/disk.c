
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "disk.h"
#include "utils.h"

/* Constants. */
#define CREATED_OFFSET          0U
#define WRITTEN_OFFSET          4U
#define READ_OFFSET             8U
#define FILENAME_OFFSET        12U
#define FILENAME_LENGTH        40U
#define SECTOR_DATA_SIZE      512U

/* Forward declarations. */
static int real_to_virtual(const struct disk *d, uint16_t rda,
                           uint16_t *vda);
static int virtual_to_real(const struct disk *d, uint16_t vda,
                           uint16_t *rda);

/* Functions. */

void disk_initvar(struct disk *d)
{
    d->sectors = NULL;
}

void disk_destroy(struct disk *d)
{
    if (d->sectors) free((void *) d->sectors);
    d->sectors = NULL;
}

int disk_create(struct disk *d, uint16_t num_cylinders,
                uint16_t num_heads, uint16_t num_sectors)
{
    size_t size;
    disk_initvar(d);

    if (unlikely(num_heads > 2
                 || num_sectors > 15
                 || num_cylinders >= 512)) {
        report_error("disk: create: invalid disk geometry");
        return FALSE;
    }

    d->num_cylinders = num_cylinders;
    d->num_heads = num_heads;
    d->num_sectors = num_sectors;

    d->length = num_cylinders * num_heads * num_sectors;
    size = ((size_t) d->length) * sizeof(struct sector);

    d->sectors = malloc(size);
    if (unlikely(!d->sectors)) {
        report_error("disk: create: memory exhausted");
        return FALSE;
    }

    return TRUE;
}

int disk_load_image(struct disk *d, const char *filename)
{
    FILE *fp;
    struct sector *s;
    uint16_t vda, j, meta_len;
    uint16_t w, *sector_ptr;
    int c;

    fp = fopen(filename, "rb");
    if (unlikely(!fp)) {
        report_error("disk: load_image: could not open file `%s`",
                     filename);
        return FALSE;
    }

    meta_len = offsetof(struct sector, data) / sizeof(uint16_t);
    for (vda = 0; vda < d->length; vda++) {
        s = &d->sectors[vda];

        /* Discard the first word. */
        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_premature_end;

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_premature_end;

        /* Use the loop index instead. */
        s->sector_vda = vda;

        sector_ptr = (uint16_t *) s;
        for (j = 1; j < meta_len; j++) {
            /* Process data in little endian format. */
            c = fgetc(fp);
            if (unlikely(c == EOF)) goto error_premature_end;
            w = (uint16_t) (c & 0xFF);

            c = fgetc(fp);
            if (unlikely(c == EOF)) goto error_premature_end;
            w |= (uint16_t) ((c & 0xFF) << 8);

            sector_ptr[j] = w;
        }

        for (j = 0; j < SECTOR_DATA_SIZE; j++) {
            c = fgetc(fp);
            if (unlikely(c == EOF)) goto error_premature_end;

            /* Byte swap the data here. */
            s->data[j ^ 1] = (uint8_t) c;
        }
    }

    c = fgetc(fp);
    if (unlikely(c != EOF)) {
        report_error("disk: read: extra data at end of disk");
        return FALSE;
    }

    fclose(fp);
    return TRUE;

error_premature_end:
    report_error("disk: load_image: %s: premature end of file",
                 filename);
    fclose(fp);
    return FALSE;
}

int disk_save_image(const struct disk *d, const char *filename)
{
    FILE *fp;
    const struct sector *s;
    uint16_t vda, j, meta_len, w;
    const uint16_t *sector_ptr;
    int c;

    fp = fopen(filename, "wb");
    if (unlikely(!fp)) {
        report_error("disk: save_image: could not open file "
                     "`%s` for writing",
                     filename);
        return FALSE;
    }

    meta_len = offsetof(struct sector, data) / sizeof(uint16_t);
    for (vda = 0; vda < d->length; vda++) {
        s = &d->sectors[vda];

        /* Discard the first word. */
        c = fputc((int) (vda & 0xFF), fp);
        if (unlikely(c == EOF)) goto error_write;

        c = fputc((int) ((vda >> 8) & 0xFF), fp);
        if (unlikely(c == EOF)) goto error_write;

        sector_ptr = (const uint16_t *) s;
        for (j = 0; j < meta_len; j++) {
            w = sector_ptr[j];

            /* Process data in little endian format. */
            c = fputc((int) (w & 0xFF), fp);
            if (unlikely(c == EOF)) goto error_write;

            c = fputc((int) ((w >> 8) & 0xFF), fp);
            if (unlikely(c == EOF)) goto error_write;
        }

        for (j = 0; j < SECTOR_DATA_SIZE; j++) {
            /* Byte swap the data here. */
            c = fputc((int) s->data[j ^ 1], fp);
            if (unlikely(c == EOF)) goto error_write;
        }
    }

    fclose(fp);
    return TRUE;

error_write:
    report_error("disk: save_image: %s: error while writing",
                 filename);
    fclose(fp);
    return FALSE;
}

int disk_check_integrity(const struct disk *d)
{
    const struct sector *s, *other_s;
    uint16_t vda, rda, other_vda;
    uint8_t slen;
    int success;

    success = TRUE;
    for (vda = 0; vda < d->length; vda++) {
        s = &d->sectors[vda];

        if (unlikely(!virtual_to_real(d, vda, &rda))) {
            report_error("disk: check_integrity: "
                         "could not convert virtual to real address");
            return FALSE;
        }

        if (s->header[1] != rda || s->header[0] != 0) {
            report_error("disk: check_integrity: "
                         "invalid sector header at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (s->label.fid[0] == 0xFFFF) continue;
        if (s->label.fid[0] != 1) {
            report_error("disk: check_integrity: "
                         "invalid label fid presence at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (s->label.fid[1] != 0x8000 && s->label.fid[1] != 0) {
            report_error("disk: check_integrity: "
                         "invalid label fid type at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (s->label.nbytes > SECTOR_DATA_SIZE) {
            report_error("disk: check_integrity: "
                         "invalid label used bytes at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (s->label.prev_rda != 0) {
            if (!real_to_virtual(d, s->label.prev_rda, &other_vda)) {
                report_error("disk: check_integrity: "
                             "invalid prev_rda at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            other_s = &d->sectors[other_vda];
            if (other_s->label.file_secnum + 1 != s->label.file_secnum) {
                report_error("disk: check_integrity: "
                             "discontiguous file_secnum (backwards) "
                             "at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (other_s->label.fid[2] != s->label.fid[2]) {
                report_error("disk: check_integrity: "
                             "differing file ids (backwards) "
                             "at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            /* First sector is special, so no test it. */
            if (other_s->label.next_rda != rda && vda != 0) {
                report_error("disk: check_integrity: "
                             "broken link (backwards) at VDA = %u",
                             vda);
                success = FALSE;
                continue;
            }
        } else {
            if (s->label.nbytes < SECTOR_DATA_SIZE) {
                report_error("disk: check_integrity: "
                             "short leader sector at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (s->label.file_secnum != 0) {
                report_error("disk: check_integrity: "
                             "file_secnum is not zero at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            slen = s->data[FILENAME_OFFSET];
            if (slen == 0 || slen >= FILENAME_LENGTH) {
                report_error("disk: check_integrity: "
                             "invalid name at VDA = %u", vda);
                success = FALSE;
                continue;
            }
        }

        if (s->label.next_rda != 0) {
            if (s->label.nbytes < SECTOR_DATA_SIZE) {
                report_error("disk: check_integrity: "
                             "short sector at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (!real_to_virtual(d, s->label.next_rda, &other_vda)) {
                report_error("disk: check_integrity: "
                             "invalid next_rda at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            other_s = &d->sectors[other_vda];
            if (other_s->label.file_secnum != s->label.file_secnum + 1) {
                report_error("disk: check_integrity: "
                             "discontiguous file_secnum (forward) "
                             "at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (other_s->label.fid[2] != s->label.fid[2]) {
                report_error("disk: check_integrity: "
                             "differing file ids (forward) "
                             "at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            /* First sector is special, so no test it. */
            if (other_s->label.prev_rda != rda && vda != 0) {
                report_error("disk: check_integrity: "
                             "broken link (forward) at VDA = %u", vda);
                success = FALSE;
                continue;
            }
        }
    }

    return success;
}

int disk_find_file(const struct disk *d, const char *name,
                   uint16_t *leader_vda)
{
    uint16_t vda;
    uint8_t slen;
    const struct sector *s;

    for (vda = 0; vda < d->length; vda++) {
        s = &d->sectors[vda];
        if (s->label.file_secnum != 0) continue;
        if (s->label.fid[0] != 1) continue;

        slen = s->data[FILENAME_OFFSET];
        if (slen == 0) continue;
        if (slen >= FILENAME_LENGTH)
            slen = FILENAME_LENGTH - 1;
        if (strncmp((const char *) &s->data[FILENAME_OFFSET + 1],
                    name, (size_t) (slen - 1)) != 0) continue;

        *leader_vda = vda;
        return TRUE;
    }

    return FALSE;
}

int disk_extract_file(const struct disk *d, uint16_t leader_vda,
                      const char *filename, int include_leader)
{
    FILE *fp;
    const struct sector *s;
    uint16_t vda, rda;
    size_t nbytes;
    size_t ret;

    if (unlikely(leader_vda >= d->length)) {
        report_error("disk: extract_file: leader_vda");
        return FALSE;
    }

    fp = fopen(filename, "wb");
    if (unlikely(!fp)) {
        report_error("disk: extract_file: could not open "
                     "`%s` for writing",
                     filename);
        return FALSE;
    }

    vda = leader_vda;
    while (vda != 0) {
        s = &d->sectors[vda];

        if (vda != leader_vda || include_leader) {
            nbytes = (size_t) s->label.nbytes;
            if (nbytes >= SECTOR_DATA_SIZE)
                nbytes = SECTOR_DATA_SIZE;
            ret = fwrite(s->data, 1, nbytes, fp);
            if (unlikely(ret != nbytes)) {
                report_error("disk: extract_file: error while writing `%s`",
                             filename);
                fclose(fp);
                return FALSE;
            }
        }

        rda = s->label.next_rda;
        if (unlikely(!real_to_virtual(d, rda, &vda))) {
            report_error("disk: extract_file: could not get next sector (%u)",
                         vda);
            fclose(fp);
            return FALSE;
        }
    }

    fclose(fp);
    return TRUE;
}

int disk_file_length(const struct disk *d, uint16_t leader_vda,
                     size_t *length)
{
    const struct sector *s;
    uint16_t vda, rda;
    size_t l;

    if (leader_vda >= d->length) {
        report_error("disk: file_length: invalid leader_vda");
        return FALSE;
    }

    l = 0;
    vda = leader_vda;
    while (vda != 0) {
        s = &d->sectors[vda];
        if (vda != leader_vda)
            l += (size_t) s->label.nbytes;

        rda = s->label.next_rda;
        if (unlikely(!real_to_virtual(d, rda, &vda))) {
            report_error("disk: file_length: error computing length");
            return FALSE;
        }
    }

    *length = l;
    return TRUE;
}

/* Obtains a time_t from the Alto filesystem. */
static
time_t get_alto_time(const uint8_t *src)
{
    time_t time;

    time = (int) src[3];
    time += (((int) src[2]) << 8);
    time += (((int) src[1]) << 16);
    time += (((int) src[0]) << 24);

    time += 2117503696; /* magic value to convert to Unix epoch. */
    return time;
}

int disk_file_times(const struct disk *d, uint16_t leader_vda,
                    time_t *created, time_t *written, time_t *read)
{
    const struct sector *s;

    if (leader_vda >= d->length) {
        report_error("disk: file_times: invalid leader_vda");
        return FALSE;
    }

    s = &d->sectors[leader_vda];
    *created = get_alto_time(&s->data[CREATED_OFFSET]);
    *written = get_alto_time(&s->data[WRITTEN_OFFSET]);
    *read = get_alto_time(&s->data[READ_OFFSET]);
    return TRUE;
}

int disk_print_summary(const struct disk *d)
{
    uint16_t vda, file_id;
    const struct sector *s;
    char namebuf[FILENAME_LENGTH];
    time_t created, written, read;
    uint8_t slen;
    size_t filelen;
    struct tm *ltm;

    printf("VDA    ID     SIZE   CREATED             FILENAME\n");
    for (vda = 0; vda < d->length; vda++) {
        s = &d->sectors[vda];
        if (s->label.file_secnum != 0) continue;
        if (s->label.fid[0] != 1) continue;

        memcpy(namebuf, (char *) &s->data[FILENAME_OFFSET],
               FILENAME_LENGTH);
        slen = s->data[FILENAME_OFFSET];
        if (slen >= FILENAME_LENGTH)
            slen = FILENAME_LENGTH - 1;
        namebuf[slen] = '\0';
        file_id = s->label.fid[2];

        if (unlikely(!disk_file_times(d, vda, &created, &written, &read))) {
            report_error("disk: print_summary: could not determine "
                         "file times");
            return FALSE;
        }

        if (unlikely(!disk_file_length(d, vda, &filelen))) {
            report_error("disk: print_summary: could not determine "
                         "file length");
            return FALSE;
        }

        ltm = localtime(&created);
        printf("%-6u %-6u %-6u %02d-%02d-%02d %2d:%02d:%02d %-38s\n",
               vda, file_id, (unsigned int) filelen,
               ltm->tm_mday, ltm->tm_mon + 1,
               ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min,
               ltm->tm_sec, &namebuf[1]);
    }

    return TRUE;
}

int disk_print_directory(const struct disk *d, uint16_t leader_vda)
{
    uint16_t vda, rda, w;
    uint16_t pos1, size1, pos2, tocopy;
    uint16_t fid_type, file_id, version, file_leader_vda;
    const struct sector *s;
    char namebuf[FILENAME_LENGTH];
    uint8_t buffer[64];
    uint8_t slen;
    int is_valid;

    if (leader_vda >= d->length) {
        report_error("disk: print_directory: invalid leader_vda");
        return FALSE;
    }

    printf("VDA    ID     VERSION  TYPE  FILENAME\n");

    vda = leader_vda;
    pos1 = 0;
    size1 = 2;
    is_valid = TRUE;
    while (vda != 0) {
        s = &d->sectors[vda];

        if (vda != leader_vda) {
            pos2 = 0;
            while (pos2 < s->label.nbytes) {
                tocopy = size1 - pos1;
                if (tocopy > s->label.nbytes - pos2)
                    tocopy = s->label.nbytes - pos2;
                if (is_valid)
                    memcpy(&buffer[pos1], &s->data[pos2], tocopy);
                pos2 += tocopy;
                pos1 += tocopy;

                if (pos1 == size1) {
                    if (size1 == 2) {
                        w = (buffer[0] << 8) + buffer[1];
                        is_valid = ((w >> 10) == 1);
                        size1 = 2 * (w & 0x3FF);
                        if (unlikely(size1 >= sizeof(buffer) && is_valid)) {
                            report_error("disk: print_directory: large "
                                         "directory entry");
                            return FALSE;
                        }
                    } else {
                        if (is_valid) {
                            memcpy(namebuf, (const char *) &buffer[12],
                                   FILENAME_LENGTH);

                            slen = (uint8_t) namebuf[0];
                            if (slen >= FILENAME_LENGTH)
                                slen = FILENAME_LENGTH - 1;
                            namebuf[slen] = '\0';

                            fid_type = buffer[3] + (buffer[2] << 8);
                            file_id = buffer[5] + (buffer[4] << 8);
                            version = buffer[7] + (buffer[6] << 8);
                            file_leader_vda = buffer[11] + (buffer[10] << 8);

                            printf("%-6u %-6u %-3u      %s     %-38s\n",
                                   file_leader_vda, file_id, version,
                                   (fid_type == 0) ? "f" : "d", &namebuf[1]);
                        }

                        pos1 = 0;
                        size1 = 2;
                        is_valid = TRUE;
                    }
                }
            }
        }

        rda = s->label.next_rda;
        if (unlikely(!real_to_virtual(d, rda, &vda))) {
            report_error("disk: print_directory: could not get "
                         "next sector (%u)", vda);
            return FALSE;
        }
    }

    return TRUE;
}

/* Converts a real address to a virtual address.
 * The real address is in `rda` and the virtual address is returned
 * in the `vda` parameter.
 * Returns TRUE on success.
 */
static
int real_to_virtual(const struct disk *d, uint16_t rda, uint16_t *vda)
{
    uint16_t cylinder, head, sector;

    cylinder = (rda >> 3) & 0x1FF;
    head = (rda >> 2) & 1;
    sector = (rda >> 12) & 0xF;

    if ((cylinder >= d->num_cylinders) || (head >= d->num_heads)
        || (sector >= d->num_sectors || ((rda & 3) != 0)))
        return FALSE;

    *vda = ((cylinder * d->num_heads) + head) * d->num_sectors + sector;
    return TRUE;
}

/* Converts a virtual address to a real address.
 * The virtual address is in `vda` and the real address is returned
 * in the `rda` parameter.
 * Returns TRUE on success.
 */
static
int virtual_to_real(const struct disk *d, uint16_t vda, uint16_t *rda)
{
    uint16_t i, cylinder, head, sector;

    if (vda >= d->length) return FALSE;

    i = vda;
    sector = i % d->num_sectors;
    i /= d->num_sectors;
    head = i % d->num_heads;
    i /= d->num_heads;
    cylinder = i;

    *rda = (cylinder << 3) | (head << 2) | (sector << 12);
    return TRUE;
}
