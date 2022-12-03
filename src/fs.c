
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "fs.h"
#include "utils.h"

/* Constants. */

/* Offsets within the page. */
#define PAGE_HEADER        offsetof(struct page, header)
#define PAGE_LABEL          offsetof(struct page, label)
#define PAGE_DATA            offsetof(struct page, data)

/* Offsets within the leader page data. */
#define LEADER_CREATED                                0U
#define LEADER_WRITTEN                                4U
#define LEADER_READ                                   8U
#define LEADER_FILENAME                              12U

/* Offsets within the directory entry. */
#define DIRECTORY_SN                                  2U
#define DIRECTORY_VERSION                             6U
#define DIRECTORY_LEADER_VDA                          6U
#define DIRECTORY_FILENAME                           12U

/* Other constants. */
#define FILE_TYPE_REGULAR                             0U
#define FILE_TYPE_DIRECTORY                      0x8000U
#define FILE_PRESENT                                  1U
#define FILE_MISSING                             0xFFFFU
#define DIR_ENTRY_VALID                               1U
#define DIR_ENTRY_MISSING                             0U
#define DIR_ENTRY_LEN_MASK                        0x3FFU


/* Forward declarations. */
static int real_to_virtual(const struct fs *fs, uint16_t rda,
                           uint16_t *vda);
static int virtual_to_real(const struct fs *fs, uint16_t vda,
                           uint16_t *rda);
static void copy_name(char *dst, const char *src);
static uint16_t read_word_bs(const uint8_t *data, size_t offset);
static time_t read_alto_time(const uint8_t *data, size_t offset);

/* Functions. */

void fs_initvar(struct fs *fs)
{
    fs->pages = NULL;
}

void fs_destroy(struct fs *fs)
{
    if (fs->pages) free((void *) fs->pages);
    fs->pages = NULL;
}

int fs_create(struct fs *fs, struct geometry dg)
{
    size_t size;
    fs_initvar(fs);

    if (unlikely(dg.num_heads > 2
                 || dg.num_sectors > 15
                 || dg.num_cylinders >= 512)) {
        report_error("fs: create: invalid disk geometry");
        return FALSE;
    }

    fs->dg = dg;

    fs->length = dg.num_cylinders * dg.num_heads * dg.num_sectors;
    size = ((size_t) fs->length) * sizeof(struct page);

    fs->pages = (struct page *) malloc(size);
    if (unlikely(!fs->pages)) {
        report_error("fs: create: memory exhausted");
        return FALSE;
    }

    return TRUE;
}

int fs_load_image(struct fs *fs, const char *filename)
{
    FILE *fp;
    struct page *pg;
    uint16_t vda, j, meta_len;
    uint16_t w, *meta_ptr;
    int c;

    fp = fopen(filename, "rb");
    if (unlikely(!fp)) return FALSE;

    meta_len = PAGE_DATA / sizeof(uint16_t);
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error;

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error;

        /* Discard the first word and use the loop index instead. */
        pg->page_vda = vda;

        meta_ptr = (uint16_t *) pg;
        for (j = 1; j < meta_len; j++) {
            /* Process data in little endian format. */
            c = fgetc(fp);
            if (unlikely(c == EOF)) goto error;
            w = (uint16_t) (c & 0xFF);

            c = fgetc(fp);
            if (unlikely(c == EOF)) goto error;
            w |= (uint16_t) ((c & 0xFF) << 8);

            meta_ptr[j] = w;
        }

        for (j = 0; j < PAGE_DATA_SIZE; j++) {
            c = fgetc(fp);
            if (unlikely(c == EOF)) goto error;

            /* Byte swap the data here. */
            pg->data[j ^ 1] = (uint8_t) c;
        }
    }

    c = fgetc(fp);
    if (unlikely(c != EOF)) return FALSE;

    fclose(fp);
    return TRUE;

error:
    fclose(fp);
    return FALSE;
}

int fs_save_image(const struct fs *fs, const char *filename)
{
    FILE *fp;
    const struct page *pg;
    uint16_t vda, j, meta_len, w;
    const uint16_t *meta_ptr;
    int c;

    fp = fopen(filename, "wb");
    if (unlikely(!fp)) return FALSE;

    meta_len = PAGE_DATA / sizeof(uint16_t);
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        /* Discard the first word. */
        c = fputc((int) (vda & 0xFF), fp);
        if (unlikely(c == EOF)) goto error;

        c = fputc((int) ((vda >> 8) & 0xFF), fp);
        if (unlikely(c == EOF)) goto error;

        meta_ptr = (const uint16_t *) pg;
        for (j = 0; j < meta_len; j++) {
            w = meta_ptr[j];

            /* Process data in little endian format. */
            c = fputc((int) (w & 0xFF), fp);
            if (unlikely(c == EOF)) goto error;

            c = fputc((int) ((w >> 8) & 0xFF), fp);
            if (unlikely(c == EOF)) goto error;
        }

        for (j = 0; j < PAGE_DATA_SIZE; j++) {
            /* Byte swap the data here. */
            c = fputc((int) pg->data[j ^ 1], fp);
            if (unlikely(c == EOF)) goto error;
        }
    }

    fclose(fp);
    return TRUE;

error:
    fclose(fp);
    return FALSE;
}

int fs_check_integrity(const struct fs *fs)
{
    const struct page *pg, *other_pg;
    uint16_t vda, rda, other_vda;
    uint8_t slen;
    int success;

    success = TRUE;
    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];

        if (unlikely(!virtual_to_real(fs, vda, &rda))) {
            report_error("fs: check_integrity: "
                         "could not convert virtual to real address");
            return FALSE;
        }

        if (pg->header[1] != rda || pg->header[0] != 0) {
            report_error("fs: check_integrity: "
                         "invalid sector header at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (pg->label.presence == FILE_MISSING) continue;
        if (pg->label.presence != FILE_PRESENT) {
            report_error("fs: check_integrity: "
                         "invalid label presence at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (pg->label.sn.file_type != FILE_TYPE_REGULAR
            && pg->label.sn.file_type != FILE_TYPE_DIRECTORY) {
            report_error("fs: check_integrity: "
                         "invalid label file type at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (pg->label.nbytes > PAGE_DATA_SIZE) {
            report_error("fs: check_integrity: "
                         "invalid label used bytes at VDA = %u", vda);
            success = FALSE;
            continue;
        }

        if (pg->label.prev_rda != 0) {
            if (!real_to_virtual(fs, pg->label.prev_rda, &other_vda)) {
                report_error("fs: check_integrity: "
                             "invalid prev_rda at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            other_pg = &fs->pages[other_vda];
            if (other_pg->label.file_pgnum + 1 != pg->label.file_pgnum) {
                report_error("fs: check_integrity: "
                             "discontiguous file_pgnum (backwards) "
                             "at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (other_pg->label.sn.file_type != pg->label.sn.file_type
                || other_pg->label.sn.file_id != pg->label.sn.file_id) {
                report_error("fs: check_integrity: "
                             "differing file serial numbers (backwards) "
                             "at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            /* First sector is special, so no test it. */
            if (other_pg->label.next_rda != rda && vda != 0) {
                report_error("fs: check_integrity: "
                             "broken link (backwards) at VDA = %u",
                             vda);
                success = FALSE;
                continue;
            }
        } else {
            if (pg->label.nbytes < PAGE_DATA_SIZE) {
                report_error("fs: check_integrity: "
                             "short leader sector at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (pg->label.file_pgnum != 0) {
                report_error("fs: check_integrity: "
                             "file_pgnum is not zero at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            slen = pg->data[LEADER_FILENAME];
            if (slen == 0 || slen >= FILENAME_LENGTH) {
                report_error("fs: check_integrity: "
                             "invalid name at VDA = %u", vda);
                success = FALSE;
                continue;
            }
        }

        if (pg->label.next_rda != 0) {
            if (pg->label.nbytes < PAGE_DATA_SIZE) {
                report_error("fs: check_integrity: "
                             "short sector at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (!real_to_virtual(fs, pg->label.next_rda, &other_vda)) {
                report_error("fs: check_integrity: "
                             "invalid next_rda at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            other_pg = &fs->pages[other_vda];
            if (other_pg->label.file_pgnum != pg->label.file_pgnum + 1) {
                report_error("fs: check_integrity: "
                             "discontiguous file_pgnum (forward) "
                             "at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            if (other_pg->label.sn.file_type != pg->label.sn.file_type
                || other_pg->label.sn.file_id != pg->label.sn.file_id) {
                report_error("fs: check_integrity: "
                             "differing file serial numbers (forward) "
                             "at VDA = %u", vda);
                success = FALSE;
                continue;
            }

            /* First sector is special, so no test it. */
            if (other_pg->label.prev_rda != rda && vda != 0) {
                report_error("fs: check_integrity: "
                             "broken link (forward) at VDA = %u", vda);
                success = FALSE;
                continue;
            }
        }
    }

    return success;
}

int fs_find_file(const struct fs *fs, const char *name,
                 struct file_entry *fe)
{
    const struct page *pg;
    uint16_t vda;
    uint8_t slen;

    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];
        if (pg->label.file_pgnum != 0) continue;
        if (pg->label.presence != FILE_PRESENT) continue;

        slen = pg->data[LEADER_FILENAME];
        if (slen == 0) continue;
        if (slen >= FILENAME_LENGTH)
            slen = FILENAME_LENGTH - 1;
        if (strncmp((const char *) &pg->data[LEADER_FILENAME + 1],
                    name, (size_t) (slen - 1)) != 0) continue;

        fe->sn = pg->label.sn;
        fe->version = 1; /* TODO: populate this. */
        fe->unused = 0;
        fe->leader_vda = vda;
        return TRUE;
    }

    return FALSE;
}

int fs_open(const struct fs *fs, const struct file_entry *fe,
            struct open_file *of)
{
    const struct page *pg;
    uint16_t rda;

    if (unlikely(fe->leader_vda >= fs->length))
        return FALSE;

    of->fe = *fe;
    of->pos.pgnum = 1;
    of->pos.pos = 0;

    pg = &fs->pages[fe->leader_vda];

    rda = pg->label.next_rda;
    if (unlikely(!real_to_virtual(fs, rda, &of->pos.vda)))
        return FALSE;

    return TRUE;
}

size_t fs_read(const struct fs *fs, struct open_file *of,
               uint8_t *dst, size_t len)
{
    const struct page *pg;
    uint16_t vda, rda, nbytes;
    size_t pos;

    pos = 0;
    while (len > 0) {
        vda = of->pos.vda;

        /* Checks if reached the end of the file. */
        if (vda == 0) break;
        if (unlikely(vda >= fs->length)) break;

        pg = &fs->pages[vda];

        /* Sanity check. */
        if (unlikely(pg->label.file_pgnum != of->pos.pgnum))
            break;

        if (of->pos.pos < pg->label.nbytes) {
            nbytes = pg->label.nbytes - of->pos.pos;
            if (nbytes > len) nbytes = len;

            if (dst) {
                memcpy(&dst[pos], &pg->data[of->pos.pos],
                       nbytes);
            }

            of->pos.pos += nbytes;
            pos += nbytes;
            len -= nbytes;
        } else if (unlikely(of->pos.pos > pg->label.nbytes)) {
            break;
        } else {
            rda = pg->label.next_rda;
            if (unlikely(!real_to_virtual(fs, rda, &of->pos.vda)))
                break;
            of->pos.pos = 0;
            if (of->pos.vda != 0)
                of->pos.pgnum += 1;
            else
                of->pos.pgnum = 0;
        }
    }

    return pos;
}

int fs_extract_file(const struct fs *fs, const struct file_entry *fe,
                    const char *output_filename)
{
    uint8_t buffer[PAGE_DATA_SIZE];
    struct open_file of;
    FILE *fp;
    size_t nbytes;
    size_t ret;

    if (unlikely(!fs_open(fs, fe, &of)))
        return FALSE;

    fp = fopen(output_filename, "wb");
    if (unlikely(!fp))
        return FALSE;

    while (TRUE) {
        nbytes = fs_read(fs, &of, buffer, sizeof(buffer));

        if (nbytes > 0) {
            ret = fwrite(buffer, 1, nbytes, fp);
            if (unlikely(ret != nbytes)) {
                fclose(fp);
                return FALSE;
            }
        }

        if (nbytes < sizeof(buffer)) break;
    }

    fclose(fp);
    return TRUE;
}

int fs_file_entry(const struct fs *fs, uint16_t leader_vda,
                  struct file_entry *fe)
{
    const struct page *pg;

    if (leader_vda >= fs->length)
        return FALSE;

    pg = &fs->pages[leader_vda];
    fe->sn = pg->label.sn;
    fe->version = 1; /* TODO: populate this. */
    fe->unused = 0;
    fe->leader_vda = leader_vda;
    return TRUE;
}

int fs_file_length(const struct fs *fs, const struct file_entry *fe,
                   size_t *length)
{
    struct open_file of;
    size_t l, nbytes;

    if (unlikely(!fs_open(fs, fe, &of)))
        return FALSE;

    l = 0;
    while (TRUE) {
        nbytes = fs_read(fs, &of, NULL, PAGE_DATA_SIZE);
        l += nbytes;
        if (nbytes != PAGE_DATA_SIZE) break;
    }

    *length = l;
    return TRUE;
}

int fs_file_info(const struct fs *fs, const struct file_entry *fe,
                 struct file_info *finfo)
{
    const struct page *pg;

    if (fe->leader_vda >= fs->length)
        return FALSE;

    pg = &fs->pages[fe->leader_vda];
    copy_name(finfo->filename, (const char *) &pg->data[LEADER_FILENAME]);
    finfo->created = read_alto_time(pg->data, LEADER_CREATED);
    finfo->written = read_alto_time(pg->data, LEADER_WRITTEN);
    finfo->read = read_alto_time(pg->data, LEADER_READ);
    return TRUE;
}

int fs_scan_files(const struct fs *fs, scan_files_cb cb, void *arg)
{
    uint16_t vda;
    const struct page *pg;
    struct file_entry fe;
    int ret;

    for (vda = 0; vda < fs->length; vda++) {
        pg = &fs->pages[vda];
        if (pg->label.file_pgnum != 0) continue;
        if (pg->label.presence != FILE_PRESENT) continue;

        fe.sn = pg->label.sn;
        fe.version = 1; /* TODO: populate this. */
        fe.unused = 0;
        fe.leader_vda = vda;

        ret = cb(fs, &fe, arg);
        if (ret < 0) return FALSE;
        if (ret == 0) break;
    }

    return TRUE;
}

int fs_scan_directory(const struct fs *fs, const struct file_entry *fe,
                      scan_directory_cb cb, void *arg)
{
    struct directory_entry de;
    struct open_file of;
    uint16_t w, de_len;
    uint8_t buffer[128];
    size_t to_read, nbytes;
    int ret, is_valid;

    if (unlikely(!fs_open(fs, fe, &of)))
        return FALSE;

    while (TRUE) {
        nbytes = fs_read(fs, &of, buffer, 2);
        if (nbytes == 0) break;
        if (nbytes != 2) return FALSE;

        w = read_word_bs(buffer, 0);
        is_valid = ((w >> 10) == DIR_ENTRY_VALID);

        de_len = (w & DIR_ENTRY_LEN_MASK);
        if (de_len == 0) return FALSE;

        to_read = 2 * ((size_t) de_len);
        if (to_read > sizeof(buffer)) {
            nbytes = fs_read(fs, &of, &buffer[2], sizeof(buffer) - 2);
            if (nbytes != sizeof(buffer) - 2) return FALSE;

            to_read -= sizeof(buffer);
            nbytes = fs_read(fs, &of, NULL, to_read);
            if (nbytes != to_read) return FALSE;
        } else {
            nbytes = fs_read(fs, &of, &buffer[2], to_read - 2);
            if (nbytes != to_read - 2) return FALSE;
        }

        if (!is_valid) continue;

        de.fe.sn.file_type = read_word_bs(buffer, DIRECTORY_SN);
        de.fe.sn.file_id = read_word_bs(buffer, 2 + DIRECTORY_SN);
        de.fe.version = read_word_bs(buffer, DIRECTORY_VERSION);
        de.fe.leader_vda = read_word_bs(buffer, DIRECTORY_LEADER_VDA);
        copy_name(de.filename, (const char *) &buffer[DIRECTORY_FILENAME]);

        ret = cb(fs, &de, arg);
        if (ret < 0) return FALSE;
        if (ret == 0) break;
    }

    return TRUE;
}

/* Converts a real address to a virtual address.
 * The real address is in `rda` and the virtual address is returned
 * in the `vda` parameter.
 * Returns TRUE on success.
 */
static
int real_to_virtual(const struct fs *fs, uint16_t rda, uint16_t *vda)
{
    uint16_t cylinder, head, sector;
    const struct geometry *dg;

    cylinder = (rda >> 3) & 0x1FF;
    head = (rda >> 2) & 1;
    sector = (rda >> 12) & 0xF;

    dg = &fs->dg;
    if ((cylinder >= dg->num_cylinders) || (head >= dg->num_heads)
        || (sector >= dg->num_sectors || ((rda & 3) != 0)))
        return FALSE;

    *vda = ((cylinder * dg->num_heads) + head) * dg->num_sectors + sector;
    return TRUE;
}

/* Converts a virtual address to a real address.
 * The virtual address is in `vda` and the real address is returned
 * in the `rda` parameter.
 * Returns TRUE on success.
 */
static
int virtual_to_real(const struct fs *fs, uint16_t vda, uint16_t *rda)
{
    uint16_t i, cylinder, head, sector;
    const struct geometry *dg;

    if (vda >= fs->length) return FALSE;
    dg = &fs->dg;

    i = vda;
    sector = i % dg->num_sectors;
    i /= dg->num_sectors;
    head = i % dg->num_heads;
    i /= dg->num_heads;
    cylinder = i;

    *rda = (cylinder << 3) | (head << 2) | (sector << 12);
    return TRUE;
}

/* Copies the filename to `dst` and set the proper
 * NUL byte at the end of the string.
 */
static
void copy_name(char *dst, const char *src)
{
    uint8_t slen;

    slen = (uint8_t) src[0];
    if (slen >= FILENAME_LENGTH)
        slen = FILENAME_LENGTH - 1;

    if (slen == 0) {
        dst[0] = '\0';
        return;
    }

    memcpy(dst, &src[1], ((size_t) slen) - 1);
    dst[slen - 1] = '\0';
}

/* Reads a word (in big endian format).
 * The source data is given by `data`, and the offset where
 * the word is in `offset`.
 * Returns the word.
 */
static
uint16_t read_word_bs(const uint8_t *data, size_t offset)
{
    uint16_t w;
    w = (uint16_t) (data[offset + 1]);
    w |= (uint16_t) (data[offset] << 8);
    return w;
}

/* Obtains a time_t from the Alto filesystem.
 * The alto data is located at `offset` in `data`.
 * Returns the time.
 */
static
time_t read_alto_time(const uint8_t *data, size_t offset)
{
    time_t time;

    time = (int) read_word_bs(data, offset + 2);
    time += ((int) read_word_bs(data, offset)) << 16;

    time += 2117503696; /* magic value to convert to Unix epoch. */
    return time;
}
