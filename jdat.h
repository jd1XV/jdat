#ifndef JDAT_DEFS_H
#define JDAT_DEFS_H

#define JD_LIB_WIN32
#include <jd_lib.h>

#define jdat_ElementByArg(type, val) (PacketElementData){ .type = val }

#define HEADER_MESSAGE_SIZE KILOBYTES(1)

typedef enum CommandStatus {
    COMMAND_STATUS_INPROGRESS,
    COMMAND_STATUS_FAILED,
    COMMAND_STATUS_SUCCEEDED,
    COMMAND_STATUS_COUNT
} CommandStatus;

typedef enum CommandCode {
    COMMAND_SEARCH,
    COMMAND_INSPECT,
    COMMAND_DOWNLOAD,
    COMMAND_CODE_COUNT
} CommandCode;

typedef struct Command {
    u32 command_code;
    u32 command_flags;
    jd_StrA command_string;
    CommandStatus status;
} Command;

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
    s64 S64;
    s32 S32;
    f64 F64;
    f32 F32;
    b32 B32;
    c8  C8;
    jd_StrA str;
} PacketElementData;

typedef struct PacketElement {
    jd_StrA key;
    PacketElementValueType value_type;
    PacketElementData data;
} PacketElement;

#define PACKET_HEADER_MAX_ELEMENTS 16

typedef struct PacketHeader {
    jd_StrA tag;
    u64 num_elements;
    PacketElement* elements[PACKET_HEADER_MAX_ELEMENTS];
    u64 text_size;
    jd_Arena* arena;
    struct PacketHeader* next;
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
    PacketHeader* last;
    jd_Arena* arena;
} jdat_Packet;

jdat_Packet* PacketCreate(jd_Arena* arena);
PacketHeader* PacketHeaderPushBack(jdat_Packet* packet, jd_StrA tag);
PacketElement* PacketElementPushBack(PacketHeader* header, PacketElement* in_element);
PacketElement* PacketElementPushBackInPlace(PacketHeader* header, PacketElement* in_element);
PacketElement* PacketElementPushBackByArg(PacketHeader* header, jd_StrA key, PacketElementValueType type, PacketElementData data);
void PacketSetError(jdat_Packet* packet, PacketErrorCode code, c8 missing_char, u32 error_index);
jdat_Packet* PacketParse(jd_Arena* arena, jd_StrA packet_string);
jd_StrA PacketToString(jd_Arena* arena, jdat_Packet* packet, jd_ArenaStr* arena_str);
u64 PacketCalcStringLength(jdat_Packet* packet);
PacketHeader* PacketGetFirstHeaderWithTag(jdat_Packet* packet, jd_StrA tag);
PacketHeader* PacketGetNextHeaderWithTag(PacketHeader* starting_header, jd_StrA tag);
PacketElement* PacketGetElementWithKey(PacketHeader* header, jd_StrA key);
void PacketJoinToBack(jdat_Packet* to_packet, jdat_Packet* from_packet);
b32 PacketCopyToBack(jd_Arena* arena, jdat_Packet* to_packet, jdat_Packet* from_packet);
PacketHeader* PacketHeaderCopy(jd_Arena* arena, PacketHeader* src);
b32 PacketHeaderAppendToArenaStr(PacketHeader* header, jd_ArenaStr* arena_str, b32 limit_value_string_len, u64 max_value_string_len);

u64     PacketElementGetU64(PacketElement* packet_element);
u32     PacketElementGetU32(PacketElement* packet_element);
s64     PacketElementGetS64(PacketElement* packet_element);
s32     PacketElementGetS32(PacketElement* packet_element);
f64     PacketElementGetF64(PacketElement* packet_element);
f32     PacketElementGetF32(PacketElement* packet_element);
b32     PacketElementGetB32(PacketElement* packet_element);
c8      PacketElementGetC8(PacketElement* packet_element);
jd_StrA PacketElementGetString(PacketElement* packet_element);

typedef struct jd_StrACompressed {
    jd_StrA str;
    u64 decompressed_size;
} jd_StrACompressed;

jd_StrACompressed StringCompress(jd_Arena* arena, jd_StrA src);
jd_StrA StringDecompress(jd_Arena* arena, jd_StrACompressed src);
u64 StringCalcCompressedLength(u64 count);


#endif // JDAT_DEFS_H
