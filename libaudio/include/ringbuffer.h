#pragma once

#include <cstddef>
#include <cstring>
#include <cstdlib>

#include <cmath>
#include <cstdint>

#define FullMemoryBarrier()  __sync_synchronize()
#define ReadMemoryBarrier()  __sync_synchronize()
#define WriteMemoryBarrier() __sync_synchronize()

#include <stdio.h>
static uint32_t nextPowerOfTwoFP(uint32_t n) {
    if (n == 0) return 1;
    uint32_t size = 1u << static_cast<uint32_t>(std::ceil(std::log2(n)));
    return size;
}

class IringBuffer
{
    size_t  m_buffer_size; /**< Number of elements in FIFO. Power of 2. **/
    size_t  m_big_mask;    /**< Used for wrapping indices with extra bit to distinguish full/empty. */
    size_t  m_small_mask;  /**< Used for fitting indices to buffer. */
    size_t  m_element_size_bytes; /**< Number of bytes per element. */
    char    *m_buffer;    /**< Pointer to the buffer containing the actual data. */
    volatile size_t  m_write_index; /**< Index of next writable element. **/
    volatile size_t  m_read_index;  /**< Index of next readable element. **/
public:
    IringBuffer(size_t elementSizeBytes, size_t elementCount)
    {
        m_buffer = nullptr;
        elementCount = nextPowerOfTwoFP(elementCount);
        m_buffer_size = elementCount;
        m_buffer = (char *)malloc( elementSizeBytes * elementCount );
        flush();
        m_big_mask = (elementCount*2)-1;
        m_small_mask = (elementCount)-1;
        m_element_size_bytes = elementSizeBytes;
    }
 
    virtual ~IringBuffer()
    {
        free(m_buffer);
    }

    size_t getBufferSize()
    {
        return m_buffer_size;
    }

    void flush()
    {
        m_write_index = 0;
        m_read_index = 0;
    }

    size_t getReadAvailable()
    {
        return ( (m_write_index - m_read_index) & m_big_mask );
    }

    size_t getWriteAvailable()
    {
        return ( m_buffer_size - getReadAvailable() );
    }

    size_t write( const void *data, size_t elementCount )
    {
        size_t size1, size2, numWritten;
        void *data1, *data2;
        numWritten = get_write_regions( elementCount, &data1, &size1, &data2, &size2 );
        if( size2 > 0 )
        {

            memcpy( data1, data, size1*m_element_size_bytes );
            data = ((char *)data) + size1*m_element_size_bytes;
            memcpy( data2, data, size2*m_element_size_bytes );
        }
        else
        {
            memcpy( data1, data, size1*m_element_size_bytes );
        }
        advance_write_index( numWritten );
        return numWritten;
    }

    size_t read( void *data, size_t elementCount )
    {
        size_t size1, size2, numRead;
        void *data1, *data2;
        numRead = get_read_regions( elementCount, &data1, &size1, &data2, &size2 );
        if( size2 > 0 )
        {
            memcpy( data, data1, size1*m_element_size_bytes );
            data = ((char *)data) + size1*m_element_size_bytes;
            memcpy( data, data2, size2*m_element_size_bytes );
        }
        else
        {
            memcpy( data, data1, size1*m_element_size_bytes );
        }
        advance_read_index( numRead );
        return numRead;
    }

private:
    size_t get_write_regions( size_t elementCount,
                            void **dataPtr1, size_t *sizePtr1,
                            void **dataPtr2, size_t *sizePtr2 )
    {
        size_t   index;
        size_t   available = getWriteAvailable();
        if( elementCount > available ) elementCount = available;
        /* Check to see if write is not contiguous. */
        index = m_write_index & m_small_mask;
        if( (index + elementCount) > m_buffer_size )
        {
            /* Write data in two blocks that wrap the buffer. */
            size_t   firstHalf = m_buffer_size - index;
            *dataPtr1 = &m_buffer[index*m_element_size_bytes];
            *sizePtr1 = firstHalf;
            *dataPtr2 = &m_buffer[0];
            *sizePtr2 = elementCount - firstHalf;
        }
        else
        {
            *dataPtr1 = &m_buffer[index*m_element_size_bytes];
            *sizePtr1 = elementCount;
            *dataPtr2 = NULL;
            *sizePtr2 = 0;
        }

        if( available )
            FullMemoryBarrier(); /* (write-after-read) => full barrier */

        return elementCount;
    }

    size_t advance_write_index( size_t elementCount )
    {
        /* ensure that previous writes are seen before we update the write index
        (write after write)
        */
        WriteMemoryBarrier();
        return m_write_index = (m_write_index + elementCount) & m_big_mask;
    }

    size_t get_read_regions( size_t elementCount,
                            void **dataPtr1, size_t *sizePtr1,
                            void **dataPtr2, size_t *sizePtr2 )
    {
        size_t   index;
        size_t   available = getReadAvailable(); /* doesn't use memory barrier */
        if( elementCount > available ) elementCount = available;
        /* Check to see if read is not contiguous. */
        index = m_read_index & m_small_mask;
        if( (index + elementCount) > m_buffer_size )
        {
            /* Write data in two blocks that wrap the buffer. */
            size_t firstHalf = m_buffer_size - index;
            *dataPtr1 = &m_buffer[index*m_element_size_bytes];
            *sizePtr1 = firstHalf;
            *dataPtr2 = &m_buffer[0];
            *sizePtr2 = elementCount - firstHalf;
        }
        else
        {
            *dataPtr1 = &m_buffer[index*m_element_size_bytes];
            *sizePtr1 = elementCount;
            *dataPtr2 = NULL;
            *sizePtr2 = 0;
        }

        if( available )
            ReadMemoryBarrier(); /* (read-after-read) => read barrier */

        return elementCount;
    }

    size_t advance_read_index( size_t elementCount )
    {
        /* ensure that previous reads (copies out of the ring buffer) are always completed before updating (writing) the read index.
        (write-after-read) => full barrier
        */
        FullMemoryBarrier();
        return m_read_index = (m_read_index + elementCount) & m_big_mask;
    }
};

template<class T>
class ringBuffer : public IringBuffer
{
public:
    ringBuffer(size_t size) : IringBuffer(sizeof(T), size){};
    virtual ~ringBuffer(){};
};
