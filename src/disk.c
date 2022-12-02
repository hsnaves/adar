
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "disk.h"
#include "utils.h"

/* Constants. */
#define CREATED_OFFSET          0
#define WRITTEN_OFFSET          4
#define READ_OFFSET             8
#define FILENAME_OFFSET        12
#define FILENAME_LENGTH        40

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
    uint16_t i, j, max_j;
    uint16_t w, *sector_ptr;
    uint8_t b;
    size_t ret;
    int c;

    fp = fopen(filename, "rb");
    if (unlikely(!fp)) {
        report_error("disk: load_image: could not open file `%s`",
                     filename);
        return FALSE;
    }

    max_j = offsetof(struct sector, data) / sizeof(uint16_t);
    for (i = 0; i < d->length; i++) {
        s = &d->sectors[i];

        /* Discard the first word. */
        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_premature_end;

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_premature_end;

        sector_ptr = (uint16_t *) s;
        for (j = 0; j < max_j; j++) {
            /* Process data in little endian format. */
            c = fgetc(fp);
            if (unlikely(c == EOF)) goto error_premature_end;
            b = (uint8_t) c;
            w = (uint16_t) b;

            c = fgetc(fp);
            if (unlikely(c == EOF)) goto error_premature_end;
            b = (uint8_t) c;
            w |= ((uint16_t) b) << 8;

            sector_ptr[j] = w;
        }

        ret = fread(s->data, 1, sizeof(s->data), fp);
        if (ret != sizeof(s->data)) goto error_premature_end;
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
    uint16_t i, j, max_j, w;
    const uint16_t *sector_ptr;
    uint8_t b;
    size_t ret;
    int c;

    fp = fopen(filename, "wb");
    if (unlikely(!fp)) {
        report_error("disk: save_image: could not open file "
                     "`%s` for writing",
                     filename);
        return FALSE;
    }

    max_j = offsetof(struct sector, data) / sizeof(uint16_t);
    for (i = 0; i < d->length; i++) {
        s = &d->sectors[i];

        /* Discard the first word. */
        c = fputc(0, fp);
        if (unlikely(c == EOF)) goto error_write;

        c = fputc(0, fp);
        if (unlikely(c == EOF)) goto error_write;

        sector_ptr = (const uint16_t *) s;
        for (j = 0; j < max_j; j++) {
            w = sector_ptr[j];

            /* Process data in little endian format. */
            b = (uint8_t) w;
            c = fputc((int) b, fp);
            if (unlikely(c == EOF)) goto error_write;

            b = (uint8_t) (w >> 8);
            c = fputc((int) b, fp);
            if (unlikely(c == EOF)) goto error_write;
        }

        ret = fwrite(s->data, 1, sizeof(s->data), fp);
        if (ret != sizeof(s->data)) goto error_write;
    }

    fclose(fp);
    return TRUE;

error_write:
    report_error("disk: save_image: %s: error while writing",
                 filename);
    fclose(fp);
    return FALSE;
}

/* Swaps the bytes from the source `src` and writes them
 * to the destination `dst`. The number of words to copy
 * is given by `nwords`.
 */
static
void bswap(char *dst, const char *src, uint16_t nwords)
{
    uint16_t j;
    size_t k;

    for (j = 0; j < nwords; j++) {
        k = (size_t) j;
        k += k;

        dst[k] = src[k + 1];
        dst[k + 1] = src[k];
    }
}

/* Translates a name (perform byte swapping).
 * The source is pointed by `src` and the destination by `dst`.
 * The size of `dst` should be at least FILENAME_LENGTH.
 */
static
void bswap_name(char *dst, const char *src)
{
    unsigned int j;

    bswap(dst, src, FILENAME_LENGTH / 2);

    j = (unsigned int) dst[0];
    if (j >= FILENAME_LENGTH)
        j = FILENAME_LENGTH - 1;
    dst[j] = '\0';
}

int disk_check_integrity(const struct disk *d)
{
    const struct sector *s, *other_s;
    uint16_t vda, rda, other_vda;
    char buffer[FILENAME_LENGTH];
    int success;

    success = TRUE;
    for (vda = 0; vda < d->length; vda++) {
        s = &d->sectors[vda];

        if (unlikely(!disk_virtual_to_real(d, vda, &rda))) {
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

        if (s->label.nbytes > 512) {
            report_error("disk: check_integrity: "
                         "invalid label used bytes at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (s->label.prev_rda != 0) {
            if (!disk_real_to_virtual(d, s->label.prev_rda, &other_vda)) {
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
            if (s->label.nbytes < 512) {
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

            bswap_name(buffer, (char *) &s->data[FILENAME_OFFSET]);
            if (((unsigned int) buffer[0]) >= FILENAME_LENGTH) {
                report_error("disk: check_integrity: "
                             "long name at VDA = %u", vda);
                success = FALSE;
                continue;
            }
        }

        if (s->label.next_rda != 0) {
            if (s->label.nbytes < 512) {
                report_error("disk: check_integrity: "
                             "short sector at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (!disk_real_to_virtual(d, s->label.next_rda, &other_vda)) {
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
    const struct sector *s;
    char buffer[FILENAME_LENGTH];

    for (vda = 0; vda < d->length; vda++) {
        s = &d->sectors[vda];
        if (s->label.file_secnum != 0) continue;
        if (s->label.fid[0] != 1) continue;

        bswap_name(buffer, (char *) &s->data[FILENAME_OFFSET]);
        if (strcmp(&buffer[1], name) != 0) continue;

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
    char buffer[512];
    uint16_t vda, rda;
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
            bswap(buffer, (const char *) s->data, 256);
            ret = fwrite(buffer, 1, s->label.nbytes, fp);
            if (unlikely(ret != s->label.nbytes)) {
                report_error("disk: extract_file: error while writing `%s`",
                             filename);
                fclose(fp);
                return FALSE;
            }
        }

        rda = s->label.next_rda;
        if (unlikely(!disk_real_to_virtual(d, rda, &vda))) {
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
                     uint16_t *length)
{
    const struct sector *s;
    uint16_t vda, rda, l;

    if (leader_vda >= d->length) {
        report_error("disk: file_length: invalid leader_vda");
        return FALSE;
    }

    l = 0;
    vda = leader_vda;
    while (vda != 0) {
        s = &d->sectors[vda];
        if (vda != leader_vda)
            l += s->label.nbytes;

        rda = s->label.next_rda;
        if (unlikely(!disk_real_to_virtual(d, rda, &vda))) {
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

    time = (int) src[2];
    time += (((int) src[3]) << 8);
    time += (((int) src[0]) << 16);
    time += (((int) src[1]) << 24);

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
    uint16_t vda, file_id, filelen;
    const struct sector *s;
    char buffer[FILENAME_LENGTH];
    time_t created, written, read;
    struct tm *ltm;

    printf("VDA    ID     SIZE   CREATED             FILENAME\n");
    for (vda = 0; vda < d->length; vda++) {
        s = &d->sectors[vda];
        if (s->label.file_secnum != 0) continue;
        if (s->label.fid[0] != 1) continue;

        bswap_name(buffer, (char *) &s->data[FILENAME_OFFSET]);
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
               vda, file_id, filelen, ltm->tm_mday, ltm->tm_mon + 1,
               ltm->tm_year % 100, ltm->tm_hour, ltm->tm_min, ltm->tm_sec,
               &buffer[1]);
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
                        w = buffer[0] + (buffer[1] << 8);
                        is_valid = ((w >> 10) == 1);
                        size1 = 2 * (w & 0x3FF);
                        if (unlikely(size1 >= sizeof(buffer) && is_valid)) {
                            report_error("disk: print_directory: large "
                                         "directory entry");
                            return FALSE;
                        }
                    } else {
                        if (is_valid) {
                            bswap_name(namebuf, (const char *) &buffer[12]);

                            fid_type = buffer[2] + (buffer[3] << 8);
                            file_id = buffer[4] + (buffer[5] << 8);
                            version = buffer[6] + (buffer[7] << 8);
                            file_leader_vda = buffer[10] + (buffer[11] << 8);

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
        if (unlikely(!disk_real_to_virtual(d, rda, &vda))) {
            report_error("disk: print_directory: could not get "
                         "next sector (%u)", vda);
            return FALSE;
        }
    }

    return TRUE;
}

int disk_real_to_virtual(const struct disk *d, uint16_t rda, uint16_t *vda)
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

int disk_virtual_to_real(const struct disk *d, uint16_t vda, uint16_t *rda)
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


