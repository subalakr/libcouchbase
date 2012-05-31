/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2011 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include "config.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <libcouchbase/couchbase.h>
#include "ringbuffer.h"

#ifdef DO_ABORT
#  define fail abort()
#else
#  define fail exit(EXIT_FAILURE)
#endif

#define QUOTE_(x) #x
#define QUOTE(x) QUOTE_(x)
#define AT "[" __FILE__ ":" QUOTE(__LINE__) "] "
#define err_exit(...)  \
    fprintf(stderr, AT __VA_ARGS__);  \
    fprintf(stderr, "\n");  \
    fail;

static void dump_buffer(ringbuffer_t *ring)
{
    char *begin = libcouchbase_ringbuffer_get_start(ring);
    char *end = begin + libcouchbase_ringbuffer_get_size(ring);
    char *rd = libcouchbase_ringbuffer_get_read_head(ring);
    char *wr = libcouchbase_ringbuffer_get_write_head(ring);
    char *cur;

    /* write head */
    fprintf(stderr, " ");
    for(cur = begin; cur < end; cur++) {
        if (cur == wr) {
            fprintf(stderr, "w");
        } else {
            fprintf(stderr, " ");
        }
    }
    fprintf(stderr, "\n");

    /* the buffer contents */
    fprintf(stderr, "|");
    for(cur = begin; cur < end; cur++) {
        fprintf(stderr, "%c", *cur ? *cur : '-');
    }
    fprintf(stderr, "|\n");

    /* the read head */
    fprintf(stderr, " ");
    for(cur = begin; cur < end; cur++) {
        if (cur == rd) {
            fprintf(stderr, "r");
        } else {
            fprintf(stderr, " ");
        }
    }
    fprintf(stderr, "\n");
}

static void wrapped_buffer_test(void)
{
    ringbuffer_t ring;
    char buffer[128];

    if (!libcouchbase_ringbuffer_initialize(&ring, 10)) {
        err_exit("Failed to create a 10 byte ringbuffer");
    }
    memset(libcouchbase_ringbuffer_get_start(&ring), 0, 10);
    /*  w
     * |----------|
     *  r
     */

    /* put 8 chars into the buffer */
    if (libcouchbase_ringbuffer_write(&ring, "01234567", 8) != 8) {
        err_exit("Failed to write 8 characters to buffer");
    }
    /*          w
     * |01234567--|
     *  r
     */

    /* consume first 5 chars */
    if (libcouchbase_ringbuffer_read(&ring, buffer, 5) != 5 ||
        memcmp(buffer, "01234", 5) != 0) {
        err_exit("Failed to consume first 5 characters");
    }
    /*          w
     * |-----567--|
     *       r
     */
    if (libcouchbase_ringbuffer_is_continous(&ring, RINGBUFFER_WRITE, 5) != 0) {
        err_exit("The buffer must not be continuous in write direction for 5 bytes");
    }
    if (libcouchbase_ringbuffer_is_continous(&ring, RINGBUFFER_WRITE, 2) == 0) {
        err_exit("The buffer must be continuous in write direction for 2 bytes");
    }


    /* wrapped write: write 5 more chars */
    if (libcouchbase_ringbuffer_write(&ring, "abcde", 5) != 5) {
        err_exit("Failed to write to wrapped buffer");
    }
    /*     w
     * |cde--567ab|
     *       r
     */

    if (libcouchbase_ringbuffer_is_continous(&ring, RINGBUFFER_READ, 7) != 0) {
        err_exit("The buffer must not be continuous in read direction for 7 bytes");
    }
    if (libcouchbase_ringbuffer_is_continous(&ring, RINGBUFFER_READ, 2) == 0) {
        err_exit("The buffer must be continuous in read direction for 2 bytes");
    }

    /* wrapped read: read 6 chars */
    if (libcouchbase_ringbuffer_read(&ring, buffer, 6) != 6 ||
        memcmp(buffer, "567abc", 6) != 0) {
        err_exit("Failed to read wrapped buffer");
    }
    /*     w
     * |-de-------|
     *   r
     */
}

// This is a crash I noticed while I was debugging the tap code
static void my_regression_1_test(void)
{
    ringbuffer_t ring;
    struct libcouchbase_iovec_st iov[2];
    ring.root = (void*)0x477a80;
    ring.read_head = (void*)0x47b0a3;
    ring.write_head =(void*)0x47b555;
    ring.size = 16384;
    ring.nbytes = 1202;

    libcouchbase_ringbuffer_get_iov(&ring, RINGBUFFER_WRITE, iov);
    // up to the end
    assert(iov[0].iov_base == ring.write_head);
    assert(iov[0].iov_len == 1323);
    // then from the beginning
    assert(iov[1].iov_base == ring.root);
    assert(iov[1].iov_len == 13859);
}

static void replace_test(void)
{
    ringbuffer_t rb;

    if (!libcouchbase_ringbuffer_initialize(&rb, 16)) {
        err_exit("Failed to create a 16 byte ringbuffer");
    }
    if (libcouchbase_ringbuffer_write(&rb, "01234567", 8) != 8 ||
        memcmp(rb.root, "01234567\0\0\0\0\0\0\0\0", rb.size) != 0) {
        err_exit("Failed to write 8 characters to buffer");
    }
    /*          w
     * |01234567--------|
     *  r
     */

    if (libcouchbase_ringbuffer_update(&rb, RINGBUFFER_READ, "ab", 2) != 2 ||
        rb.nbytes != 8 || memcmp(rb.root, "ab234567\0\0\0\0\0\0\0\0", rb.size) != 0) {
        err_exit("Failed to replace 2 characters at READ end of the buffer");
    }
    /*          w
     * |ab234567--------|
     *  r
     */

    if (libcouchbase_ringbuffer_update(&rb, RINGBUFFER_WRITE, "cd", 2) != 2 ||
        rb.nbytes != 8 || memcmp(rb.root, "ab2345cd\0\0\0\0\0\0\0\0", rb.size) != 0) {
        err_exit("Failed to replace 2 characters at WRITE end of the buffer");
    }
    /*          w
     * |ab2345cd--------|
     *  r
     */

    libcouchbase_ringbuffer_consumed(&rb, 3);
    if (rb.nbytes != 5 || rb.read_head != rb.root + 3) {
        err_exit("Failed to read 3 characters from the buffer");
    }
    /*          w
     * |ab2345cd--------|
     *     r
     */

    if (libcouchbase_ringbuffer_update(&rb, RINGBUFFER_READ, "efghij", 6) != 5 ||
        rb.nbytes != 5 || memcmp(rb.root, "ab2efghi\0\0\0\0\0\0\0\0", rb.size) != 0) {
        err_exit("Failed to replace 5 characters at READ end of the buffer");
    }
    /*          w
     * |ab2efghi--------|
     *     r
     */

    if (libcouchbase_ringbuffer_update(&rb, RINGBUFFER_WRITE, "klmnop", 6) != 5 ||
        rb.nbytes != 5 || memcmp(rb.root, "ab2klmno\0\0\0\0\0\0\0\0", rb.size) != 0) {
        err_exit("Failed to replace 5 characters at WRITE end of the buffer");
    }
    /*          w
     * |ab2klmno--------|
     *     r
     */

    if (libcouchbase_ringbuffer_write(&rb, "0123456789", 10) != 10 ||
        rb.nbytes != 15 || memcmp(rb.root, "892klmno01234567", rb.size) != 0) {
        err_exit("Failed to write 10 characters into the buffer");
    }
    /*    w
     * |892klmno01234567|
     *     r
     */

    if (libcouchbase_ringbuffer_update(&rb, RINGBUFFER_WRITE, "abcdefghij", 10) != 10 ||
        rb.nbytes != 15 || memcmp(rb.root, "ij2klmnoabcdefgh", rb.size) != 0) {
        err_exit("Failed to replace 10 characters at WRITE end of the buffer");
    }
    /*    w
     * |ij2klmnoabcdefgh|
     *     r
     */

    libcouchbase_ringbuffer_consumed(&rb, 6);
    if (rb.nbytes != 9 || rb.read_head != rb.root + 9) {
        err_exit("Failed to read 6 characters from the buffer");
    }
    /*    w
     * |ij2klmnoabcdefgh|
     *           r
     */

    if (libcouchbase_ringbuffer_update(&rb, RINGBUFFER_READ, "12345678", 8) != 8 ||
        rb.nbytes != 9 || memcmp(rb.root, "8j2klmnoa1234567", rb.size) != 0) {
        err_exit("Failed to replace 10 characters at READ end of the buffer");
    }
    /*    w
     * |8j2klmnoa1234567|
     *           r
     */
}

static void memcpy_test(void)
{
    char buffer[1024];
    ringbuffer_t src, dst;

    if (!libcouchbase_ringbuffer_initialize(&src, 16)) {
        err_exit("Failed to create a 16 byte ringbuffer");
    }
    if (libcouchbase_ringbuffer_write(&src, "01234567", 8) != 8) {
        err_exit("Failed to write 8 characters to buffer");
    }

    if (!libcouchbase_ringbuffer_initialize(&dst, 16)) {
        err_exit("Failed to create a 16 byte ringbuffer");
    }
    if (libcouchbase_ringbuffer_memcpy(&dst, &src, 4) != 0) {
        err_exit("Failed to copy 4 characters to buffer");
    }
    if (dst.nbytes != 4) {
        err_exit("The buffer should contain 4 bytes instead of %d", (int)dst.nbytes);
    }
    if (libcouchbase_ringbuffer_read(&dst, buffer, 4) != 4 ||
        memcmp(buffer, "0123", 4) != 0) {
        err_exit("Failed to read dest buffer");
    }
}

int main(int argc, char **argv)
{
    ringbuffer_t ring;
    char buffer[1024];
    int ii;

    /* use dump_buffer() to display buffer contents */
    (void)dump_buffer;
    (void)argc; (void)argv;

    if (!libcouchbase_ringbuffer_initialize(&ring, 16)) {
        err_exit("Failed to create a 16 byte ringbuffer");
    }

    if (libcouchbase_ringbuffer_read(&ring, buffer, 1) != 0) {
        err_exit("Read from an empty buffer should return 0");
    }

    if (libcouchbase_ringbuffer_write(&ring, "01234567891234567", 17) != 16) {
        err_exit("Buffer overflow!!!");
    }

    for (ii = 0; ii < 2; ++ii) {
        memset(buffer, 0, sizeof(buffer));
        if (libcouchbase_ringbuffer_peek(&ring, buffer, 16) != 16 ||
            memcmp(buffer, "01234567891234567", 16) != 0) {
            err_exit("We just filled the buffer with 16 bytes.. peek failed");
        }
    }

    if (libcouchbase_ringbuffer_read(&ring, buffer, 16) != 16) {
        err_exit("We just filled the buffer with 16 bytes");
    }

    if (libcouchbase_ringbuffer_read(&ring, buffer, 1) != 0) {
        err_exit("Read from an empty buffer should return 0");
    }

    if (libcouchbase_ringbuffer_write(&ring, "01234567891234567", 17) != 16) {
        err_exit("Buffer overflow!!!");
    }

    if (libcouchbase_ringbuffer_read(&ring, buffer, 8) != 8) {
        err_exit("We just filled the buffer with 16 bytes");
    }

    if (!libcouchbase_ringbuffer_ensure_capacity(&ring, 9)) {
        err_exit("I failed to grow the buffer");
    }

    if (ring.size != 32) {
        err_exit("The buffers should double in size");
    }

    if (ring.read_head != ring.root) {
        err_exit("I expected the data to be realigned");
    }

    if (libcouchbase_ringbuffer_read(&ring, buffer, 8) != 8) {
        err_exit("We should still have 8 bytes left");
    }

    if (memcmp(buffer, "89123456", 8) != 0) {
        err_exit("I'm not getting the data I'm expecting...");
    }

    wrapped_buffer_test();
    my_regression_1_test();
    memcpy_test();
    replace_test();

    return 0;
}
