#ifndef GDELTA_INTERNAL_H
#define GDELTA_INTERNAL_H

#include <type_traits>

#pragma pack(push, 1)
/*
 * ABI:
 *
 * VarInt<N>: more 1 | pval N [more| VarInt<7>]
 * DeltaHead: flag 1 | VarInt<6>
 * DeltaUnit: DeltaHead [DeltaHead.flag| VarInt<7>]
 *
 * VarInt <- Val, Offset = Val | VarInt[i].pval << Offset, Offset + VarInt[i]::N
 */
template <uint8_t FLAGLEN> 
struct _DeltaHead {
  uint8_t flag: FLAGLEN;
  uint8_t more: 1;
  uint8_t length: (7-FLAGLEN);
  const static uint8_t lenbits = FLAGLEN;
};

typedef _DeltaHead<1> DeltaHeadUnit;

typedef struct _VarIntPart {
  uint8_t more: 1;
  uint8_t subint: 7;
  const static uint8_t lenbits = 7;
} VarIntPart;

#pragma pack(pop)

typedef struct {
  uint8_t flag;
  uint64_t length;
  uint64_t offset;
} DeltaUnitMem;

// DeltaUnit/FlaggedVarInt: flag: 1, more: 1, len: 6
// VarInt: more: 1, len: 7
static_assert(sizeof(DeltaHeadUnit) == 1, "Expected DeltaHeads to be 1 byte");
static_assert(sizeof(VarIntPart) == 1, "Expected VarInt to be 1 byte");


#define DEBUG_UNITS 0

typedef struct {
  uint8_t *buf;
  uint64_t cursor;
  uint64_t length;
} BufferStreamDescriptor;

typedef struct {
  const uint8_t *buf;
  uint64_t cursor;
  uint64_t length;
} ReadOnlyBufferStreamDescriptor;

template <typename B>
void ensure_stream_length(B &stream, size_t length) {
  if constexpr (!std::is_const<decltype(stream.buf)>::value) {
    if (length > stream.length) {
      stream.buf = (uint8_t*)realloc(stream.buf, length);
      stream.length = length;
    }
  }
}

template <typename B, typename T>
void write_field(B &buffer, const T &field) {
  static_assert(!std::is_const<decltype(buffer.buf)>::value, "Stream needs to be writeable for write_field");
  ensure_stream_length(buffer, buffer.cursor + sizeof(T));
  memcpy(buffer.buf + buffer.cursor, &field, sizeof(T));
  buffer.cursor += sizeof(T);
  // TODO: check bounds (buffer->length)?
}


template <typename B, typename T>
void read_field(B &buffer, T& field) {  
  memcpy(&field, buffer.buf + buffer.cursor, sizeof(T));
  buffer.cursor += sizeof(T);
  // TODO: check bounds (buffer->length)?
}


template <typename DstT, typename SrcT>
void stream_into(DstT &dest, SrcT &src, size_t length) {
  static_assert(!std::is_const<decltype(dest.buf)>::value, "Stream needs to be writeable for write_field");
  ensure_stream_length(dest, dest.cursor + length);
  memcpy(dest.buf + dest.cursor, src.buf + src.cursor, length);
  dest.cursor += length;
  src.cursor += length;
}

template <typename DstT, typename SrcT>
void stream_from(DstT &dest, const SrcT &src, size_t src_cursor, size_t length) {
  static_assert(!std::is_const<decltype(dest.buf)>::value, "Stream needs to be writeable for write_field");
  ensure_stream_length(dest, dest.cursor + length);
  memcpy(dest.buf + dest.cursor, src.buf + src_cursor, length);
  dest.cursor += length;
}

template <typename DstT, typename SrcT>
void write_concat_buffer(DstT &dest, const SrcT &src) {
  static_assert(!std::is_const<decltype(dest.buf)>::value, "Stream needs to be writeable for write_field");
  ensure_stream_length(dest, dest.cursor + src.cursor + 1);
  memcpy(dest.buf + dest.cursor, src.buf, src.cursor);
  dest.cursor += src.cursor;
}

template <typename B>
uint64_t read_varint(B& buffer) {
  VarIntPart vi;
  uint64_t val = 0;
  uint8_t offset = 0;
  do {
    read_field(buffer, vi);
    val |= vi.subint << offset;
    offset += VarIntPart::lenbits;
  } while(vi.more);
  return val;
}

template <typename B>
void read_unit(B& buffer, DeltaUnitMem& unit) {
  DeltaHeadUnit head;
  read_field(buffer, head);
 
  unit.flag = head.flag;
  unit.length = head.length;
  if (head.more) {
    unit.length = read_varint(buffer) << DeltaHeadUnit::lenbits | unit.length;
  }
  if (head.flag) {
    unit.offset = read_varint(buffer);
  }
#if DEBUG_UNITS
  fprintf(stderr, "Reading unit %d %zu %zu\n", unit.flag, unit.length, unit.offset);
#endif
}

const uint8_t varint_mask = ((1 << VarIntPart::lenbits) -1);
const uint8_t head_varint_mask = ((1 << DeltaHeadUnit::lenbits) -1);
template <typename B>
void write_varint(B& buffer, uint64_t val) 
{
  static_assert(!std::is_const<decltype(buffer.buf)>::value, "Stream needs to be writeable for write_field");
  VarIntPart vi;
  do {
    vi.subint = val & varint_mask;
    val >>= VarIntPart::lenbits;
    if (val == 0) {
      vi.more = 0;
      write_field(buffer, vi);
      break;
    }
    vi.more = 1;
    write_field(buffer, vi);
  } while (1);
}

template <typename B>
void write_unit(B& buffer, const DeltaUnitMem& unit) {
  static_assert(!std::is_const<decltype(buffer.buf)>::value, "Stream needs to be writeable for write_field");
  // TODO: Abort if length 0?
#if DEBUG_UNITS
  fprintf(stderr, "Writing unit %d %zu %zu\n", unit.flag, unit.length, unit.offset);
#endif

  DeltaHeadUnit head = {unit.flag, unit.length > head_varint_mask, (uint8_t)(unit.length & head_varint_mask)};
  write_field(buffer, head);

  uint64_t remaining_length = unit.length >> DeltaHeadUnit::lenbits;
  write_varint(buffer, remaining_length);
  if (unit.flag) {
    write_varint(buffer, unit.offset);
  }
}

#endif // GDELTA_INTERNAL_H
