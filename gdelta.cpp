#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>

#ifdef _MSC_VER
#include <compat/msvc.h>
#else
#include <ctime>
#endif

#include "gdelta_internal.h"
#include "gdelta.h"
#include "gear_matrix.h"

#define INIT_BUFFER_SIZE 128 * 1024 
#define FPTYPE uint64_t
#define STRLOOK 16
#define STRLSTEP 2

#define PRINT_PERF 0

void GFixSizeChunking(const uint8_t *data, int len, int begflag, int begsize,
                     uint32_t *hash_table, int mask) {
  if (len < STRLOOK)
    return;

  int i = 0;
  int movebitlength = sizeof(FPTYPE) * 8 / STRLOOK;
  if (sizeof(FPTYPE) * 8 % STRLOOK != 0)
    movebitlength++;
  FPTYPE fingerprint = 0;

  /** GEAR **/
  for (; i < STRLOOK; i++) {
    fingerprint = (fingerprint << (movebitlength)) + GEARmx[data[i]];
  }

  i -= STRLOOK;
  FPTYPE index = 0;
  int numChunks = len - STRLOOK + 1;

  int flag = 0;
  int _begsize = begflag ? begsize : 0;
  while (i < numChunks) {
    if (flag == STRLSTEP) {
      flag = 0;
      index = (fingerprint) >> (sizeof(FPTYPE) * 8 - mask);
      if (hash_table[index] != 0) {
        index = fingerprint >> (sizeof(FPTYPE) * 8 - mask);
      }
      hash_table[index] = i + _begsize;
    }
    /** GEAR **/
    fingerprint = (fingerprint << (movebitlength)) + GEARmx[data[i + STRLOOK]];
    i++;
    flag++;
  }

  return;
}

int gencode(const uint8_t *newBuf, uint32_t newSize, const uint8_t *baseBuf,
            uint32_t baseSize, uint8_t **deltaBuf, uint32_t *deltaSize) {
#if PRINT_PERF
  struct timespec tf0, tf1;
  clock_gettime(CLOCK_MONOTONIC, &tf0);
#endif

  /* detect the head and tail of one chunk */
  uint32_t beg = 0, end = 0, begSize = 0, endSize = 0;

  if (*deltaBuf == nullptr) {
    *deltaBuf = (uint8_t*)malloc(INIT_BUFFER_SIZE);
  }
  uint8_t* databuf = (uint8_t*)malloc(INIT_BUFFER_SIZE);
  uint8_t* instbuf = (uint8_t*)malloc(INIT_BUFFER_SIZE);

  // Find first difference 
  // First in 8 byte blocks and then in 1 byte blocks for speed
  while (begSize + sizeof(uint64_t) <= baseSize &&
         begSize + sizeof(uint64_t) <= newSize &&
         *(uint64_t *)(baseBuf + begSize) == *(uint64_t *)(newBuf + begSize)) {
    begSize += sizeof(uint64_t);
  }

  while (begSize < baseSize && 
         begSize < newSize && 
         baseBuf[begSize] == newBuf[begSize]) {
    begSize++;
  }

  if (begSize > 16)
    beg = 1;
  else
    begSize = 0;

  // Find first difference (from the end)
  while (endSize + sizeof(uint64_t) <= baseSize &&
         endSize + sizeof(uint64_t) <= newSize &&
         *(uint64_t *)(baseBuf + baseSize - endSize - sizeof(uint64_t)) ==
         *(uint64_t *)(newBuf + newSize - endSize - sizeof(uint64_t))) {
    endSize += sizeof(uint64_t);
  }

  while (endSize < baseSize && 
         endSize < newSize && 
         baseBuf[baseSize - endSize - 1] == newBuf[newSize - endSize - 1]) {
    endSize++;
  }

  if (begSize + endSize > newSize)
    endSize = newSize - begSize;

  if (endSize > 16)
    end = 1;
  else
    endSize = 0;
  /* end of detect */

  BufferStreamDescriptor deltaStream = {*deltaBuf, 0, *deltaSize};
  BufferStreamDescriptor instStream = {instbuf, 0, sizeof(instbuf)}; // Instruction stream
  BufferStreamDescriptor dataStream = {databuf, 0, sizeof(databuf)};
  ReadOnlyBufferStreamDescriptor newStream = {newBuf, begSize, newSize};
  DeltaUnitMem unit = {}; // In-memory represtation of current working unit

  if (begSize + endSize >= baseSize) { // TODO: test this path
    if (beg) {
      // Data at start is from the original file, write instruction to copy from base
      unit.flag = true;
      unit.offset = 0;
      unit.length = begSize;
      write_unit(instStream, unit);
    }
    if (newSize - begSize - endSize > 0) {
      int32_t litlen = newSize - begSize - endSize;
      unit.flag = false;
      unit.length = litlen;
      write_unit(instStream, unit);
      stream_into(dataStream, newStream, litlen);
    }
    if (end) {
      int32_t matchlen = endSize;
      int32_t offset = baseSize - endSize;
      unit.flag = true;
      unit.offset = offset;
      unit.length = matchlen;
      write_unit(instStream, unit);
    }

    write_varint(deltaStream, instStream.cursor);
    write_concat_buffer(deltaStream, instStream);
    write_concat_buffer(deltaStream, dataStream);

    *deltaSize = deltaStream.cursor; 
    *deltaBuf = deltaStream.buf;

#if PRINT_PERF
    clock_gettime(CLOCK_MONOTONIC, &tf1);
    fprintf(stderr, "gencode took: %zdns\n", (tf1.tv_sec - tf0.tv_sec) * 1000000000 + tf1.tv_nsec - tf0.tv_nsec);
#endif
    free(dataStream.buf);
    free(instStream.buf);
    return deltaStream.cursor;
  }

  /* chunk the baseFile */
  int32_t tmp = (baseSize - begSize - endSize) + 10;
  int32_t bit;
  for (bit = 0; tmp; bit++)
    tmp >>= 1;
  uint64_t xxsize = 0XFFFFFFFFFFFFFFFF >> (64 - bit); // mask
  uint32_t hash_size = xxsize + 1;
  uint32_t *hash_table = (uint32_t *)malloc(hash_size * sizeof(uint32_t));
  memset(hash_table, 0, sizeof(uint32_t) * hash_size);

#if PRINT_PERF
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
#endif

  GFixSizeChunking(baseBuf + begSize, baseSize - begSize - endSize, beg,
                   begSize, hash_table, bit);
#if PRINT_PERF
  clock_gettime(CLOCK_MONOTONIC, &t1);

  fprintf(stderr, "size:%d\n", baseSize - begSize - endSize);
  fprintf(stderr, "hash size:%d\n", hash_size);
  fprintf(stderr, "rolling hash:%.3fMB/s\n",
          (double)(baseSize - begSize - endSize) / 1024 / 1024 /
              ((t1.tv_sec - t0.tv_sec) * 1000000000 + t1.tv_nsec - t0.tv_nsec) *
              1000000000);
  fprintf(stderr, "rolling hash:%zd\n",
          (t1.tv_sec - t0.tv_sec) * 1000000000 + t1.tv_nsec - t0.tv_nsec);

  clock_gettime(CLOCK_MONOTONIC, &t0);
  fprintf(stderr, "hash table :%zd\n",
          (t0.tv_sec - t1.tv_sec) * 1000000000 + t0.tv_nsec - t1.tv_nsec);
#endif
  /* end of inserting */

  uint32_t inputPos = begSize;
  uint32_t cursor;
  int32_t movebitlength = 0;
  if (sizeof(FPTYPE) * 8 % STRLOOK == 0)
    movebitlength = sizeof(FPTYPE) * 8 / STRLOOK;
  else
    movebitlength = sizeof(FPTYPE) * 8 / STRLOOK + 1;

  if (beg) {
    // Data at start is from the original file, write instruction to copy from base
    unit.flag = true;
    unit.offset = 0;
    unit.length = begSize;
    write_unit(instStream, unit);
    unit.length = 0; // Mark as written
  }

  FPTYPE fingerprint = 0;
  for (uint32_t i = 0; i < STRLOOK && i < newSize - endSize - inputPos; i++) {
    fingerprint = (fingerprint << (movebitlength)) + GEARmx[(newBuf + inputPos)[i]];
  }

  uint32_t handlebytes = begSize;
  while (inputPos + STRLOOK <= newSize - endSize) {
    uint32_t length;
    bool matchflag = false;
    if (newSize - endSize - inputPos < STRLOOK) {
      cursor = inputPos + (newSize - endSize);
      length = newSize - endSize - inputPos;
    } else {
      cursor = inputPos + STRLOOK;
      length = STRLOOK;
    }
    int32_t index1 = fingerprint >> (sizeof(FPTYPE) * 8 - bit);
    uint32_t offset = 0;
    if (hash_table[index1] != 0 && memcmp(newBuf + inputPos, baseBuf + hash_table[index1], length) == 0) {
      matchflag = true;
      offset = hash_table[index1];
    }

    /* New data match found in hashtable/base data; attempt to create copy instruction*/
    if (matchflag) {
      // Check how much is possible to copy
      int32_t j = 0;
#if 1 /* 8-bytes optimization */
      while (offset + length + j + 7 < baseSize - endSize &&
             cursor + j + 7 < newSize - endSize && 
             *(uint64_t *)(baseBuf + offset + length + j) ==*(uint64_t *)(newBuf + cursor + j)) {
        j += sizeof(uint64_t);
      }
      while (offset + length + j < baseSize - endSize &&
             cursor + j < newSize - endSize &&
             baseBuf[offset + length + j] == newBuf[cursor + j]) {
        j++;
      }
#endif
      cursor += j;


      int32_t matchlen = cursor - inputPos;
      handlebytes += cursor - inputPos;
      uint64_t _offset = offset;


      // Check if switching modes Literal -> Copy, and dump instruction if available
      if (!unit.flag && unit.length) {
        /* Detect if end of previous literal could have been a partial copy*/
        uint32_t k = 0;
        while (k + 1 <= offset && k + 1 <= unit.length) {
          if (baseBuf[offset - (k + 1)] == newBuf[inputPos - (k + 1)])
            k++;
          else
            break;
        }

        if (k > 0) {
          // Reduce literal by the amount covered by the copy
          unit.length -= k;
          // Set up adjusted copy parameters
          matchlen += k;
          _offset -= k;
          // Last few literal bytes can be overwritten, so move cursor back
          dataStream.cursor -= k;
        }

        write_unit(instStream, unit);
        unit.length = 0; // Mark written
      }

      unit.flag = true;
      unit.offset = _offset;
      unit.length = matchlen;
      write_unit(instStream, unit);
      unit.length = 0; // Mark written


      // Update cursor (inputPos) and fingerprint
      for (uint32_t k = cursor; k < cursor + STRLOOK && cursor + STRLOOK < newSize - endSize; k++) {
        fingerprint = (fingerprint << (movebitlength)) + GEARmx[newBuf[k]];
      }
      inputPos = cursor;
    } else { // No match, need to write additional (literal) data
      /* 
       * Accumulate length one byte at a time (as literal) in unit while no match is found
       * Pre-emptively write to datastream
       */

      unit.flag = false;
      unit.length += 1;
      stream_from(dataStream, newStream, inputPos, 1);
      handlebytes += 1;


      // Update cursor (inputPos) and fingerprint
      if (inputPos + STRLOOK < newSize - endSize)
        fingerprint = (fingerprint << (movebitlength)) + GEARmx[newBuf[inputPos + STRLOOK]];
      inputPos++;
    }
  }

#if PRINT_PERF
  clock_gettime(CLOCK_MONOTONIC, &t1);
  fprintf(stderr, "look up:%zd\n",
          (t1.tv_sec - t0.tv_sec) * 1000000000 + t1.tv_nsec - t0.tv_nsec);
  fprintf(stderr, "look up:%.3fMB/s\n",
          (double)(baseSize - begSize - endSize) / 1024 / 1024 /
              ((t1.tv_sec - t0.tv_sec) * 1000000000 + t1.tv_nsec - t0.tv_nsec) *
              1000000000);
#endif

  // If last unit was unwritten literal, update it to use the rest of the data
  if (!unit.flag && unit.length) {
    newStream.cursor = handlebytes;
    stream_into(dataStream, newStream, newSize - endSize - handlebytes);

    unit.length += (newSize - endSize - handlebytes);
    write_unit(instStream, unit);
    unit.length = 0;
  } else { // Last unit was Copy, need new instruction
    if (newSize - endSize - handlebytes) {
      newStream.cursor = inputPos;
      stream_into(dataStream, newStream, newSize - endSize - handlebytes);

      unit.flag = false;
      unit.length = newSize - endSize - handlebytes;
      write_unit(instStream, unit);
      unit.length = 0;
    }
  }

  if (end) {
    int32_t matchlen = endSize;
    int32_t offset = baseSize - endSize;
     
    unit.flag = true;
    unit.offset = offset;
    unit.length = matchlen;
    write_unit(instStream, unit);
    unit.length = 0;
  }

  deltaStream.cursor = 0;
  write_varint(deltaStream, instStream.cursor);
  write_concat_buffer(deltaStream, instStream);
  write_concat_buffer(deltaStream, dataStream);
  *deltaSize = deltaStream.cursor;
  *deltaBuf = deltaStream.buf;
#if PRINT_PERF
    clock_gettime(CLOCK_MONOTONIC, &tf1);
    fprintf(stderr, "gencode took: %zdns\n", (tf1.tv_sec - tf0.tv_sec) * 1000000000 + tf1.tv_nsec - tf0.tv_nsec);
#endif
 
  free(dataStream.buf);
  free(instStream.buf);
  free(hash_table);
  return deltaStream.cursor; 
}

int gdecode(const uint8_t *deltaBuf, uint32_t deltaSize, const uint8_t *baseBuf, uint32_t baseSize,
            uint8_t **outBuf, uint32_t *outSize) {

  if (*outBuf == nullptr) {
    *outBuf = (uint8_t*)malloc(INIT_BUFFER_SIZE);
  }

#if PRINT_PERF
  struct timespec tf0, tf1;
  clock_gettime(CLOCK_MONOTONIC, &tf0);
#endif
  ReadOnlyBufferStreamDescriptor deltaStream = {deltaBuf, 0, deltaSize}; // Instructions
  const uint64_t instructionLength = read_varint(deltaStream);
  const uint64_t instOffset = deltaStream.cursor;
  ReadOnlyBufferStreamDescriptor addDeltaStream = {deltaBuf, deltaStream.cursor + instructionLength, deltaSize};
  ReadOnlyBufferStreamDescriptor baseStream = {baseBuf, 0, baseSize}; // Data in
  BufferStreamDescriptor outStream = {*outBuf, 0, *outSize};   // Data out
  DeltaUnitMem unit = {};

  while (deltaStream.cursor < instructionLength + instOffset) {
    read_unit(deltaStream, unit);
    if (unit.flag) // Read from original file using offset
      stream_from(outStream, baseStream, unit.offset, unit.length);
    else          // Read from delta file at current cursor
      stream_into(outStream, addDeltaStream, unit.length);
  }

  *outSize = outStream.cursor;
  *outBuf = outStream.buf;
#if PRINT_PERF
    clock_gettime(CLOCK_MONOTONIC, &tf1);
    fprintf(stderr, "gdecode took: %zdns\n", (tf1.tv_sec - tf0.tv_sec) * 1000000000 + tf1.tv_nsec - tf0.tv_nsec);
#endif
  return outStream.cursor;
}
