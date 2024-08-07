#ifndef JDAT_DEFS_H
#define JDAT_DEFS_H

#define JD_LIB_WIN32
#include <jd_lib.h>

#define jdat_ElementByArg(type, val) (PacketElementData){ .type = val }
#define jdat_ForEachElement(identifier, header) for (u64 identifier = 0; identifier < header->num_elements; identifier++)
#define jdat_ForEachHeader(identifier, packet) for (PacketHeader* identifier = packet->head; identifier != NULL && identifier != packet->last->next; identifier = identifier->next)

typedef enum PacketElementValueType {
    PACKET_ELEMENT_VALUE_TYPE_NULL,
    PACKET_ELEMENT_VALUE_TYPE_U64,
    PACKET_ELEMENT_VALUE_TYPE_U32,
    PACKET_ELEMENT_VALUE_TYPE_S64,
    PACKET_ELEMENT_VALUE_TYPE_S32,
    PACKET_ELEMENT_VALUE_TYPE_F64,
    PACKET_ELEMENT_VALUE_TYPE_F32,
    PACKET_ELEMENT_VALUE_TYPE_B32,
    PACKET_ELEMENT_VALUE_TYPE_C8,
    PACKET_ELEMENT_VALUE_TYPE_STRING,
    PACKET_ELEMENT_VALUE_TYPE_COUNT
} PacketElementValueType;

typedef union PacketElementData {
    u64 U64;
    u32 U32;
    i64 S64;
    i32 S32;
    f64 F64;
    f32 F32;
    b32 B32;
    c8  C8;
    jd_String str;
} PacketElementData;

typedef struct PacketElement {
    jd_String key;
    PacketElementValueType value_type;
    PacketElementData data;
} PacketElement;

#define PACKET_HEADER_MAX_ELEMENTS 16

typedef struct PacketHeader {
    jd_String tag;
    u64 num_elements;
    PacketElement* elements[PACKET_HEADER_MAX_ELEMENTS];
    u64 text_size;
    jd_Arena* arena;
    struct PacketHeader* next;
    struct PacketHeader* last;
} PacketHeader;

typedef enum PacketErrorCode {
    PACKET_INCOMPLETE_ELEMENT,
    PACKET_INCOMPLETE_HEADER,
    PACKET_UNKNOWN_TYPE,
} PacketErrorCode;

typedef struct PacketError {
    b32 success;
    PacketErrorCode code;
    c8 missing_char;
    u32 error_index;
} PacketError;

typedef struct jdat_Packet {
    PacketError error;
    PacketHeader* head;
    PacketHeader* tail;
    jd_Arena* arena;
} jdat_Packet;

jdat_Packet* PacketCreate(jd_Arena* arena);
PacketHeader* PacketHeaderPushBack(jdat_Packet* packet, jd_String tag);
PacketElement* PacketElementPushBack(PacketHeader* header, PacketElement* in_element);
PacketElement* PacketElementPushBackInPlace(PacketHeader* header, PacketElement* in_element);
PacketElement* PacketElementPushBackByArg(PacketHeader* header, jd_String key, PacketElementValueType type, PacketElementData data);

PacketElement* PacketElementPushBackString(PacketHeader* header, jd_String key, jd_String val);
PacketElement* PacketElementPushBackU64(PacketHeader* header, jd_String key, u64 val);
PacketElement* PacketElementPushBackU32(PacketHeader* header, jd_String key, u32 val);

void PacketSetError(jdat_Packet* packet, PacketErrorCode code, c8 missing_char, u32 error_index);
jdat_Packet* PacketParse(jd_Arena* arena, jd_String packet_string);
jd_String PacketToString(jd_Arena* arena, jdat_Packet* packet, jd_ArenaStr* arena_str);
u64 PacketCalcStringLength(jdat_Packet* packet);
PacketHeader* PacketGetFirstHeaderWithTag(jdat_Packet* packet, jd_String tag);
PacketHeader* PacketGetNextHeaderWithTag(PacketHeader* starting_header, jd_String tag);
PacketElement* PacketGetElementWithKey(PacketHeader* header, jd_String key);
void PacketJoinToBack(jdat_Packet* to_packet, jdat_Packet* from_packet);
b32 PacketCopyToBack(jd_Arena* arena, jdat_Packet* to_packet, jdat_Packet* from_packet);
PacketHeader* PacketHeaderCopy(jd_Arena* arena, PacketHeader* src);
b32 PacketHeaderAppendToArenaStr(PacketHeader* header, jd_ArenaStr* arena_str, b32 limit_value_string_len, u64 max_value_string_len);
void PacketHeaderPop(jdat_Packet* packet, PacketHeader* header);

u64     PacketElementGetU64(PacketElement* packet_element);
u32     PacketElementGetU32(PacketElement* packet_element);
i64     PacketElementGetS64(PacketElement* packet_element);
i32     PacketElementGetS32(PacketElement* packet_element);
f64     PacketElementGetF64(PacketElement* packet_element);
f32     PacketElementGetF32(PacketElement* packet_element);
b32     PacketElementGetB32(PacketElement* packet_element);
c8      PacketElementGetC8(PacketElement* packet_element);
jd_String PacketElementGetString(PacketElement* packet_element);

typedef struct jd_StringCompressed {
    jd_String str;
    u64 decompressed_size;
} jd_StringCompressed;

jd_StringCompressed StringCompress(jd_Arena* arena, jd_String src);
jd_String StringDecompress(jd_Arena* arena, jd_StringCompressed src);
u64 StringCalcCompressedLength(u64 count);

#endif // JDAT_DEFS_H
