#pragma once

#include <cstddef>
#include <cstring>
#include <cstdlib>

#include <cmath>
#include <cstdint>

#define FullMemoryBarrier()  __sync_synchronize()
#define ReadMemoryBarrier()  __sync_synchronize()
#define WriteMemoryBarrier() __sync_synchronize()

static uint32_t nextPowerOfTwoFP(uint32_t n) {
    if (n == 0) return 1;
    return 1u << static_cast<uint32_t>(std::ceil(std::log2(n)));
}

class ringBuffer
{
    size_t  bufferSize; /**< Number of elements in FIFO. Power of 2. **/
    volatile size_t  writeIndex; /**< Index of next writable element. **/
    volatile size_t  readIndex;  /**< Index of next readable element. **/
    size_t  bigMask;    /**< Used for wrapping indices with extra bit to distinguish full/empty. */
    size_t  smallMask;  /**< Used for fitting indices to buffer. */
    size_t  elementSizeBytes; /**< Number of bytes per element. */
    char  *buffer;    /**< Pointer to the buffer containing the actual data. */

public:
    ringBuffer(size_t elementSizeBytes, size_t elementCount)
    {
        buffer = nullptr;

        elementCount = nextPowerOfTwoFP(elementCount);
        bufferSize = elementCount;
        buffer = (char *)malloc( elementSizeBytes * elementCount );
        flush();
        bigMask = (elementCount*2)-1;
        smallMask = (elementCount)-1;
        this->elementSizeBytes = elementSizeBytes;
    }
 
    ~ringBuffer()
    {
        free(buffer);
    }

    void flush()
    {
        writeIndex = 0;
        readIndex = 0;
    }

    size_t getReadAvailable()
    {
        return ( (writeIndex - readIndex) & bigMask );
    }

    size_t getWriteAvailable()
    {
        return ( bufferSize - getReadAvailable() );
    }

    size_t write( const void *data, size_t elementCount )
    {
        size_t size1, size2, numWritten;
        void *data1, *data2;
        numWritten = getWriteRegions( elementCount, &data1, &size1, &data2, &size2 );
        if( size2 > 0 )
        {

            memcpy( data1, data, size1*elementSizeBytes );
            data = ((char *)data) + size1*elementSizeBytes;
            memcpy( data2, data, size2*elementSizeBytes );
        }
        else
        {
            memcpy( data1, data, size1*elementSizeBytes );
        }
        advanceWriteIndex( numWritten );
        return numWritten;
    }

    size_t read( void *data, size_t elementCount )
    {
        size_t size1, size2, numRead;
        void *data1, *data2;
        numRead = getReadRegions( elementCount, &data1, &size1, &data2, &size2 );
        if( size2 > 0 )
        {
            memcpy( data, data1, size1*elementSizeBytes );
            data = ((char *)data) + size1*elementSizeBytes;
            memcpy( data, data2, size2*elementSizeBytes );
        }
        else
        {
            memcpy( data, data1, size1*elementSizeBytes );
        }
        advanceReadIndex( numRead );
        return numRead;
    }

private:
    size_t getWriteRegions( size_t elementCount,
                                        void **dataPtr1, size_t *sizePtr1,
                                        void **dataPtr2, size_t *sizePtr2 )
    {
        size_t   index;
        size_t   available = getWriteAvailable();
        if( elementCount > available ) elementCount = available;
        /* Check to see if write is not contiguous. */
        index = writeIndex & smallMask;
        if( (index + elementCount) > bufferSize )
        {
            /* Write data in two blocks that wrap the buffer. */
            size_t   firstHalf = bufferSize - index;
            *dataPtr1 = &buffer[index*elementSizeBytes];
            *sizePtr1 = firstHalf;
            *dataPtr2 = &buffer[0];
            *sizePtr2 = elementCount - firstHalf;
        }
        else
        {
            *dataPtr1 = &buffer[index*elementSizeBytes];
            *sizePtr1 = elementCount;
            *dataPtr2 = NULL;
            *sizePtr2 = 0;
        }

        if( available )
            FullMemoryBarrier(); /* (write-after-read) => full barrier */

        return elementCount;
    }

    size_t advanceWriteIndex( size_t elementCount )
    {
        /* ensure that previous writes are seen before we update the write index
        (write after write)
        */
        WriteMemoryBarrier();
        return writeIndex = (writeIndex + elementCount) & bigMask;
    }

    size_t getReadRegions( size_t elementCount,
                                void **dataPtr1, size_t *sizePtr1,
                                void **dataPtr2, size_t *sizePtr2 )
    {
        size_t   index;
        size_t   available = getReadAvailable(); /* doesn't use memory barrier */
        if( elementCount > available ) elementCount = available;
        /* Check to see if read is not contiguous. */
        index = readIndex & smallMask;
        if( (index + elementCount) > bufferSize )
        {
            /* Write data in two blocks that wrap the buffer. */
            size_t firstHalf = bufferSize - index;
            *dataPtr1 = &buffer[index*elementSizeBytes];
            *sizePtr1 = firstHalf;
            *dataPtr2 = &buffer[0];
            *sizePtr2 = elementCount - firstHalf;
        }
        else
        {
            *dataPtr1 = &buffer[index*elementSizeBytes];
            *sizePtr1 = elementCount;
            *dataPtr2 = NULL;
            *sizePtr2 = 0;
        }

        if( available )
            ReadMemoryBarrier(); /* (read-after-read) => read barrier */

        return elementCount;
    }

    size_t advanceReadIndex( size_t elementCount )
    {
        /* ensure that previous reads (copies out of the ring buffer) are always completed before updating (writing) the read index.
        (write-after-read) => full barrier
        */
        FullMemoryBarrier();
        return readIndex = (readIndex + elementCount) & bigMask;
    }
};