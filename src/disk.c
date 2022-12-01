
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

int disk_create(struct disk *d, unsigned int num_cylinders,
                unsigned int num_heads, unsigned int num_sectors)
{
    size_t size;
    disk_initvar(d);

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

int disk_read(struct disk *d, const char *filename)
{
    unsigned int i, j, max_j;
    struct sector *s;
    FILE *fp;
    uint8_t b;
    uint16_t w, *sector_ptr;
    int c, ret;

    fp = fopen(filename, "rb");
    if (unlikely(!fp)) {
        report_error("disk: read: could not open file `%s`",
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
    report_error("disk: read: %s: premature end of file",
                 filename);
    fclose(fp);
    return FALSE;
}

void disk_print_summary(struct disk *d)
{
    unsigned int i, j;
    struct sector *s;
    char buffer[41];

    buffer[sizeof(buffer) - 1] = '\0';
    for (i = 0; i < d->length; i++) {
        s = &d->sectors[i];
        if (s->label.file_secnum != 0) continue;
        if (s->label.fid[0] != 1) continue;

        for (j = 0; j < FILENAME_LENGTH; j++) {
            buffer[j ^ 1] = s->data[FILENAME_OFFSET + j];
        }

        j = (unsigned int) buffer[0];
        buffer[j] = '\0';

        printf("%u) %s\n", i, &buffer[1]);
    }
}


