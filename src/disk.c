
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "disk.h"
#include "utils.h"

/* Constants. */
#define FILENAME_OFFSET     12
#define FILENAME_LENGTH     40

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
    int c, ret;

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

int disk_real_to_virtual(struct disk *d, uint16_t rda, uint16_t *vda)
{
    uint16_t cylinder, head, sector;

    cylinder = (rda >> 3) & 0x1FF;
    head = (rda >> 2) & 1;
    sector = (rda >> 12) & 0xF;

    if ((cylinder >= d->num_cylinders) || (head >= d->num_heads)
        || (sector >= d->num_sectors || ((rda & 3) != 0))) {
        report_error("disk: real_to_virtual: invalid rda = %u", rda);
        return FALSE;
    }

    *vda = ((cylinder * d->num_heads) + head) * d->num_sectors + sector;
    return TRUE;
}

int disk_virtual_to_real(struct disk *d, uint16_t vda, uint16_t *rda)
{
    uint16_t i, cylinder, head, sector;

    if (vda >= d->length) {
        report_error("disk: virtual_to_real: invalid vda = %u", vda);
        return FALSE;
    }

    i = vda;
    sector = i % d->num_sectors;
    i /= d->num_sectors;
    head = i % d->num_heads;
    i /= d->num_heads;
    cylinder = i;

    *rda = (cylinder << 3) | (head << 2) | (sector << 12);
    return TRUE;
}

int disk_file_length(struct disk *d, unsigned int first_vda, uint16_t *length)
{
    struct sector *s;
    uint16_t vda, rda, l;

    l = 0;
    vda = first_vda;
    while (vda != 0) {
        if (vda >= d->length) {
            report_error("disk: file_length: invalid sector");
            return FALSE;
        }
        s = &d->sectors[vda];
        if (vda != first_vda)
            l += s->label.nbytes;

        rda = s->label.rda_next;
        if (unlikely(!disk_real_to_virtual(d, rda, &vda))) {
            report_error("disk: file_length: error computing length");
            return FALSE;
        }
    }

    *length = l;
    return TRUE;
}

int disk_print_summary(struct disk *d)
{
    unsigned int j;
    uint16_t vda, length;
    struct sector *s;
    char buffer[41];

    buffer[sizeof(buffer) - 1] = '\0';
    printf("VDA    SIZE   FILENAME\n");
    for (vda = 0; vda < d->length; vda++) {
        s = &d->sectors[vda];
        if (s->label.file_secnum != 0) continue;
        if (s->label.fid[0] != 1) continue;

        for (j = 0; j < FILENAME_LENGTH; j++) {
            buffer[j ^ 1] = s->data[FILENAME_OFFSET + j];
        }

        j = (unsigned int) buffer[0];
        buffer[j] = '\0';

        if (unlikely(!disk_file_length(d, vda, &length))) {
            report_error("disk: print_summary: could not determine "
                         "file length");
            return FALSE;
        }

        printf("%-6u %-6u %-38s\n", vda, length, &buffer[1]);
    }

    return TRUE;
}


