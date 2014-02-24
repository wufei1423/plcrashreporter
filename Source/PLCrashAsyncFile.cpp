/*
 * Copyright (c) 2013 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "PLCrashAsyncFile.hpp"

#include <unistd.h>
#include <errno.h>

using namespace plcrash::async;

/**
 * @internal
 * @ingroup plcrash_async
 * @defgroup plcrash_async_bufio Async-safe Buffered IO
 * @{
 */

/**
 * Write len bytes to fd, looping until all bytes are written
 * or an error occurs.
 *
 * For the local file system, only one call to write() should be necessary.
 *
 * @param fd Open, writable file descriptor.
 * @param data The buffer to be written to @a fd.
 * @param len The total size of @a data, in bytes.
 */
ssize_t AsyncFile::writen (int fd, const void *data, size_t len) {
    const uint8_t *p;
    size_t left;
    ssize_t written = 0;
    
    /* Loop until all bytes are written */
    p = (const uint8_t *) data;
    left = len;
    while (left > 0) {
        if ((written = ::write(fd, p, left)) <= 0) {
            if (errno == EINTR) {
                // Try again
                written = 0;
            } else {
                return -1;
            }
        }
        
        left -= written;
        p += written;
    }
    
    return written;
}

/**
 * Construct a new AsyncFile instance.
 *
 * @param fd Open, writable file descriptor.
 * @param output_limit Maximum number of bytes that will be written to disk. Intended as a
 * safety measure prevent a run-away crash log writer from filling the disk. Specify
 * 0 to disable any limits. Once the limit is reached, all data will be dropped.
 */
AsyncFile::AsyncFile (int fd, off_t output_limit) {
    this->fd = fd;
    this->buflen = 0;
    this->total_bytes = 0;
    this->limit_bytes = output_limit;
}

/**
 * @internal
 *
 * Return the size of the internal buffer. This is the maximum number of bytes
 * that will be buffered before AsyncFile::write flushes the buffer contents
 * to disk.
 *
 * @warning This method is intended to be used by unit tests when determining
 * the minimum amount of data that must be written to trigger a buffer flush,
 * and should not be used externally.
 */
size_t AsyncFile::buffer_size (void) {
    return sizeof(this->buffer);
}

/**
 * Write all bytes from @a data to the file buffer. Returns true on success,
 * or false if an error occurs.
 *
 * @param data The buffer to be written to @a fd.
 * @param len The total size of @a data, in bytes.
 */
bool AsyncFile::write (const void *data, size_t len) {
    /* Check and update output limit */
    if (this->limit_bytes != 0 && len + this->total_bytes > this->limit_bytes) {
        return false;
    } else if (this->limit_bytes != 0) {
        this->total_bytes += len;
    }
    
    /* Check if the buffer will fill */
    if (this->buflen + len > sizeof(this->buffer)) {
        /* Flush the buffer */
        if (AsyncFile::writen(this->fd, this->buffer, this->buflen) < 0) {
            PLCF_DEBUG("Error occured writing to crash log: %s", strerror(errno));
            return false;
        }
        
        this->buflen = 0;
    }
    
    /* Check if the new data fits within the buffer, if so, buffer it */
    if (len + this->buflen <= sizeof(this->buffer)) {
        plcrash_async_memcpy(this->buffer + this->buflen, data, len);
        this->buflen += len;
        
        return true;
        
    } else {
        /* Won't fit in the buffer, just write it */
        if (AsyncFile::writen(this->fd, data, len) < 0) {
            PLCF_DEBUG("Error occured writing to crash log: %s", strerror(errno));
            return false;
        }
        
        return true;
    }
}


/**
 * Flush all buffered bytes from the file buffer to disk.
 */
bool AsyncFile::flush (void) {
    /* Anything to do? */
    if (this->buflen == 0)
        return true;
    
    /* Write remaining */
    if (AsyncFile::writen(this->fd, this->buffer, this->buflen) < 0) {
        PLCF_DEBUG("Error occured writing to crash log: %s", strerror(errno));
        return false;
    }
    
    this->buflen = 0;
    
    return true;
}


/**
 * Close the backing file descriptor.
 */
bool AsyncFile::close (void) {
    /* Flush any pending data */
    if (!this->flush())
        return false;
    
    /* Close the file descriptor */
    if (::close(this->fd) != 0) {
        PLCF_DEBUG("Error closing file: %s", strerror(errno));
        return false;
    }
    
    return true;
}

/**
 * @} plcrash_async_bufio
 */