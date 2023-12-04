
#include <lz4/lz4.h>
#include <lz4/lz4hc.h>
#include <lz4/lz4_all.c>

jd_StrACompressed StringCompress(jd_Arena* arena, jd_StrA src) {
    jd_StrACompressed compressed_string = {0};
    if (src.count == 0) return compressed_string;
    compressed_string.str = jd_StrACreateEmpty(arena, LZ4_compressBound(src.count));
    compressed_string.decompressed_size = src.count;
    int actual_compressed_size = LZ4_compress_default(src.val, compressed_string.str.val, src.count, compressed_string.str.count);
    compressed_string.str.count = (u64)actual_compressed_size;
    return compressed_string;
}

jd_StrA StringDecompress(jd_Arena* arena, jd_StrACompressed src) {
    if (src.str.count == 0) return src.str;
    jd_StrA string = jd_StrACreateEmpty(arena, src.decompressed_size);
    LZ4_decompress_safe(src.str.val, string.val, src.str.count, string.count);
    return string;
}

u64 StringCalcCompressedLength(u64 count) {
    return (u64)LZ4_compressBound(count);
}

static const u32 packet_type_sizes[PACKET_ELEMENT_VALUE_TYPE_COUNT] = {
    0,
    sizeof(u64),
    sizeof(u32),
    sizeof(s64),
    sizeof(s32),
    sizeof(f64),
    sizeof(f32),
    sizeof(b32),
    sizeof(c8),
    sizeof(u64) // for the str
};

static const u32 packet_type_strlens[PACKET_ELEMENT_VALUE_TYPE_COUNT] = {
    0,
    sizeof("u64:") - 1,
    sizeof("u32:") - 1,
    sizeof("s64:") - 1,
    sizeof("s32:") - 1,
    sizeof("f64:") - 1,
    sizeof("f32:") - 1,
    sizeof("b32:") - 1,
    sizeof("c8:") - 1,
    sizeof("string:") - 1
};

const jd_StrA header_windowdressing = {
    .val = "@ {\n}\n",
    .count = sizeof("@ {\n}\n") - 1
};

const jd_StrA element_windowdressing = {
    .val = " = ;\n",
    .count = sizeof(" = ;\n") - 1
};

jdat_Packet* PacketCreate(jd_Arena* arena) {
    jdat_Packet* packet = jd_ArenaAlloc(arena, sizeof(*packet));
    packet->head = NULL;
    packet->last = NULL;
    packet->arena = arena;
    packet->error.success = true;
    return packet;
}

PacketHeader* PacketHeaderPushBack(jdat_Packet* packet, jd_StrA tag) {
    PacketHeader* header = NULL;
    if (packet->head == NULL) {
        packet->head = jd_ArenaAlloc(packet->arena, sizeof(*packet->head));
        packet->last = packet->head;
        header = packet->head;
    } else {
        packet->last->next = jd_ArenaAlloc(packet->arena, sizeof(*packet->last->next));
        header = packet->last->next;
    }
    
    header->tag = jd_StrDupIgnoreChars(packet->arena, tag, jd_StrALit("\t\r\n "));
    header->num_elements = 0;
    header->text_size = tag.count + header_windowdressing.count;
    header->next = NULL;
    header->arena = packet->arena;
    packet->last = header;
    
    return header;
}

PacketElement* PacketElementPushBack(PacketHeader* header, PacketElement* in_element) {
    jd_Assert(header->num_elements < PACKET_HEADER_MAX_ELEMENTS);
    if (in_element->value_type == PACKET_ELEMENT_VALUE_TYPE_NULL || in_element->value_type == PACKET_ELEMENT_VALUE_TYPE_COUNT) return NULL;
    header->elements[header->num_elements] = jd_ArenaAlloc(header->arena, sizeof(PacketElement));
    PacketElement* element = header->elements[header->num_elements];
    header->num_elements++;
    element->key = jd_StrDupIgnoreChars(header->arena, in_element->key, jd_StrALit(" \t\n\r"));
    element->value_type = in_element->value_type;
    element->data = in_element->data;
    
    header->text_size += element->key.count;
    header->text_size += element_windowdressing.count;
    header->text_size += packet_type_strlens[element->value_type];
    header->text_size += packet_type_sizes[element->value_type];
    
    if (element->value_type == PACKET_ELEMENT_VALUE_TYPE_STRING) {
        header->text_size += in_element->data.str.count;
    }
    
    return element;
}

PacketElement* PacketElementPushBackInPlace(PacketHeader* header, PacketElement* in_element) {
    if (header->num_elements >= PACKET_HEADER_MAX_ELEMENTS) return NULL;
    if (in_element->value_type == PACKET_ELEMENT_VALUE_TYPE_NULL || in_element->value_type == PACKET_ELEMENT_VALUE_TYPE_COUNT) return NULL;
    PacketElement* element = header->elements[header->num_elements] = in_element;
    header->num_elements++;
    header->text_size += element->key.count;
    header->text_size += element_windowdressing.count;
    header->text_size += packet_type_strlens[element->value_type];
    header->text_size += packet_type_sizes[element->value_type];
    
    if (element->value_type == PACKET_ELEMENT_VALUE_TYPE_STRING) {
        header->text_size += in_element->data.str.count;
    }
    return element;
}

PacketElement* PacketElementPushBackByArg(PacketHeader* header, jd_StrA key, PacketElementValueType type, PacketElementData data) {
    PacketElement element = {
        .key = key,
        .value_type = type,
        .data = data
    };
    
    PacketElement* out_element = PacketElementPushBack(header, &element);
    return out_element;
}


PacketElement* PacketElementPushBackU64(PacketHeader* header, jd_StrA key, u64 val) {
    PacketElement element = {
        .key = key,
        .value_type = PACKET_ELEMENT_VALUE_TYPE_U64,
        .data.U64 = val
    };
    
    PacketElement* out = PacketElementPushBack(header, &element);
    return out;
}


PacketElement* PacketElementPushBackString(PacketHeader* header, jd_StrA key, jd_StrA val) {
    PacketElement element = {
        .key = key,
        .value_type = PACKET_ELEMENT_VALUE_TYPE_STRING,
        .data.str = jd_StrDup(header->arena, val),
    };
    
    PacketElement* out = PacketElementPushBack(header, &element);
    return out;
}

PacketHeader* PacketGetFirstHeaderWithTag(jdat_Packet* packet, jd_StrA tag) {
    PacketHeader* header = packet->head;
    while (header != NULL) {
        if (jd_StrMatch(header->tag, tag)) {
            return header;
        }
        
        header = header->next;
    }
    
    return header;
}

PacketHeader* PacketGetNextHeaderWithTag(PacketHeader* starting_header, jd_StrA tag) {
    PacketHeader* header = starting_header->next;
    while (header != NULL) {
        if (jd_StrMatch(header->tag, tag)) {
            return header;
        }
        
        header = header->next;
    }
    
    return header;
}

PacketElement* PacketGetElementWithKey(PacketHeader* header, jd_StrA key) {
    for (u32 i = 0; i < header->num_elements; i++) {
        if (jd_StrMatch(header->elements[i]->key, key)) {
            return header->elements[i];
        }
    }
    
    return NULL;
}


PacketElementValueType ParseElementValueType(jd_StrA value_type_str) {
    if (jd_StrContainsSubstrLit(value_type_str, "u64")) {
        return PACKET_ELEMENT_VALUE_TYPE_U64;
    }
    
    else if (jd_StrContainsSubstrLit(value_type_str, "u32")) {
        return PACKET_ELEMENT_VALUE_TYPE_U32;
    }
    else if (jd_StrContainsSubstrLit(value_type_str, "s64")) {
        return PACKET_ELEMENT_VALUE_TYPE_S64;
    }
    
    else if (jd_StrContainsSubstrLit(value_type_str, "s32")) {
        return PACKET_ELEMENT_VALUE_TYPE_S32;
    }
    
    else if (jd_StrContainsSubstrLit(value_type_str, "f64")) {
        return PACKET_ELEMENT_VALUE_TYPE_F64;
    }
    
    else if (jd_StrContainsSubstrLit(value_type_str, "f32")) {
        return PACKET_ELEMENT_VALUE_TYPE_F32;
    }
    
    else if (jd_StrContainsSubstrLit(value_type_str, "b32")) {
        return PACKET_ELEMENT_VALUE_TYPE_B32;
    }
    
    else if (jd_StrContainsSubstrLit(value_type_str, "c8")) {
        return PACKET_ELEMENT_VALUE_TYPE_C8;
    }
    
    else if (jd_StrContainsSubstrLit(value_type_str, "string")) {
        return PACKET_ELEMENT_VALUE_TYPE_STRING;
    }
    
    else return PACKET_ELEMENT_VALUE_TYPE_NULL;
}

void PacketSetError(jdat_Packet* packet, PacketErrorCode code, c8 missing_char, u32 error_index) {
    packet->error.success = false;
    packet->error.code = code;
    packet->error.missing_char = missing_char;
    packet->error.error_index = error_index;
}

jdat_Packet* PacketParse(jd_Arena* arena, jd_StrA packet_string) {
    jdat_Packet* packet = PacketCreate(arena);
    packet->error.success = true;
    PacketHeader* packet_header = NULL;
    u32 index = 0;
    u32 str_in_progress_index = 0;
    
    jd_StrA key = {
        .val = NULL,
        .count = 0
    };
    
    jd_StrA value = {
        .val = NULL,
        .count = 0
    };
    
    jd_StrA value_type_str = {
        .val = NULL,
        .count = 0
    };
    
    c8 required_char = '@';
    for (; index < packet_string.count;) {
        while (index < packet_string.count && packet_string.val[index] != required_char) {
            if (required_char == '=' && packet_string.val[index] == '}') {
                required_char = '@';
            }
            index++;
        }
        
        if (index == packet_string.count && required_char != '@') {
            if (required_char == '{' || 
                required_char == '}') {
                PacketSetError(packet, PACKET_INCOMPLETE_HEADER, required_char, index); break;
            }
            
            if (required_char == '=' || 
                required_char == ':' ||
                required_char == ';') {
                PacketSetError(packet, PACKET_INCOMPLETE_ELEMENT, required_char, index); break;
            }
        }
        
        
        if (required_char == '@') {
            str_in_progress_index = index + 1;
            required_char = '{';
        }
        
        else if (required_char == '{') {
            u64 tag_count = index - str_in_progress_index;
            if (tag_count == 0) { PacketSetError(packet, PACKET_INCOMPLETE_HEADER, required_char, index); break; } 
            jd_StrA tag = {
                .val = &packet_string.val[str_in_progress_index],
                .count = tag_count
            };
            
            str_in_progress_index = index + 1;
            packet_header = PacketHeaderPushBack(packet, tag);
            required_char = '=';
        }
        
        else if (required_char == '=') {
            u64 tag_count = index - str_in_progress_index;
            if (tag_count == 0) { PacketSetError(packet, PACKET_INCOMPLETE_ELEMENT, required_char, index); break; }
            
            key.val = &packet_string.val[str_in_progress_index];
            key.count = tag_count;
            
            str_in_progress_index = index + 1;
            required_char = ':';
        }
        
        else if (required_char == ':') {
            u64 tag_count = index - str_in_progress_index;
            if (tag_count == 0) { PacketSetError(packet, PACKET_INCOMPLETE_ELEMENT, required_char, index); break; }
            value_type_str.val = &packet_string.val[str_in_progress_index];
            value_type_str.count = tag_count;
            
            PacketElementValueType type = ParseElementValueType(value_type_str);
            PacketElement element = {0};
            element.key = key;
            element.value_type = type;
            
            u32 size = packet_type_sizes[type];
            u32 data_index = index + 1;
            if (data_index + size > packet_string.count) { PacketSetError(packet, PACKET_INCOMPLETE_ELEMENT, required_char, index); break; }
            
            if (type == PACKET_ELEMENT_VALUE_TYPE_U64) {
                jd_Assert(size == sizeof(u64));
                u64* ptr = (u64*)&packet_string.val[data_index];
                element.data.U64 = *ptr;
            }
            
            else if (type == PACKET_ELEMENT_VALUE_TYPE_U32) {
                jd_Assert(size == sizeof(u32));
                u32* ptr = (u32*)&packet_string.val[data_index];
                element.data.U32 = *ptr;
            }
            
            else if (type == PACKET_ELEMENT_VALUE_TYPE_S64) {
                jd_Assert(size == sizeof(s64));
                s64* ptr = (s64*)&packet_string.val[data_index];
                element.data.S64 = *ptr;
            }
            
            else if (type == PACKET_ELEMENT_VALUE_TYPE_S32) {
                jd_Assert(size == sizeof(s32));
                s32* ptr = (s32*)&packet_string.val[data_index];
                element.data.S32 = *ptr;
            }
            
            else if (type == PACKET_ELEMENT_VALUE_TYPE_F64) {
                jd_Assert(size == sizeof(f64));
                f64* ptr = (f64*)&packet_string.val[data_index];
                element.data.F64 = *ptr;
            }
            
            else if (type == PACKET_ELEMENT_VALUE_TYPE_F32) {
                jd_Assert(size == sizeof(f32));
                f32* ptr = (f32*)&packet_string.val[data_index];
                element.data.F32 = *ptr;
            }
            
            else if (type == PACKET_ELEMENT_VALUE_TYPE_B32) {
                jd_Assert(size == sizeof(b32));
                b32* ptr = (b32*)&packet_string.val[data_index];
                element.data.B32 = *ptr;
            }
            
            else if (type == PACKET_ELEMENT_VALUE_TYPE_C8) {
                jd_Assert(size == sizeof(c8));
                c8* ptr = (c8*)&packet_string.val[data_index];
                element.data.C8 = *ptr;
            }
            
            else if (type == PACKET_ELEMENT_VALUE_TYPE_STRING) {
                jd_Assert(size == sizeof(u64));
                if (data_index + size > packet_string.count) { PacketSetError(packet, PACKET_INCOMPLETE_ELEMENT, required_char, index); break; }
                u64  str_index = data_index + sizeof(u64);
                u64* str_len_ptr = (u64*)&packet_string.val[data_index];
                
                u64 count = *str_len_ptr;
                if (count + str_index > packet_string.count) { PacketSetError(packet, PACKET_INCOMPLETE_ELEMENT, required_char, index); break; }
                jd_StrA data_str = {
                    .count = count,
                    .val = &packet_string.val[str_index]
                };
                
                element.data.str = jd_StrDup(arena, data_str);
                index += count;
            }
            
            else {
                PacketSetError(packet, PACKET_UNKNOWN_TYPE, required_char, index); 
                break;
            }
            
            index += size;
            
            PacketElementPushBack(packet_header, &element);
            required_char = ';';
        }
        
        else if (required_char == ';') {
            str_in_progress_index = index + 1;
            required_char = '=';
        }
        
    }
    
    return packet;
}

jd_StrA PacketToString(jd_Arena* arena, jdat_Packet* packet, jd_ArenaStr* arena_str) {
    jd_StrA element_type_strings[PACKET_ELEMENT_VALUE_TYPE_COUNT] = {
        jd_StrALit("NULL"),
        jd_StrALit("u64:"),
        jd_StrALit("u32:"),
        jd_StrALit("s64:"),
        jd_StrALit("s32:"),
        jd_StrALit("f64:"),
        jd_StrALit("f32:"),
        jd_StrALit("b32:"),
        jd_StrALit("c8:"),
        jd_StrALit("string:")
    };
    
    b32 use_passed_arenastr = true;
    jd_ArenaStr* packet_str = NULL;
    if (arena_str == NULL) {
        packet_str = jd_ArenaStrCreate(0, MEGABYTES(16));
        use_passed_arenastr = false;
    }
    else {
        packet_str = arena_str;
    }
    
    jd_StrA o_bracket_s = jd_StrALit(" {\n");
    jd_StrA equals_s = jd_StrALit(" = ");
    jd_StrA semic_s = jd_StrALit(";\n");
    jd_StrA c_bracket_s = jd_StrALit("}\n");
    
    PacketHeader* header = packet->head;
    while (header != NULL) {
        if (header->num_elements == 0) header = header->next;
        b32 at_w = jd_ArenaStrAppendC8(packet_str, '@');
        b32 tag_w = jd_ArenaStrAppendStr(packet_str, header->tag);
        b32 o_brac_w = jd_ArenaStrAppendStr(packet_str, o_bracket_s);
        
        for (u32 i = 0; i < header->num_elements; i++) {
            b32 key_w = jd_ArenaStrAppendStr(packet_str, header->elements[i]->key);
            b32 equal_w = jd_ArenaStrAppendStr(packet_str, equals_s);
            b32 type_s_w = jd_ArenaStrAppendStr(packet_str, element_type_strings[header->elements[i]->value_type]);
            switch (header->elements[i]->value_type) {
                case PACKET_ELEMENT_VALUE_TYPE_U64:
                case PACKET_ELEMENT_VALUE_TYPE_U32:
                case PACKET_ELEMENT_VALUE_TYPE_S64:
                case PACKET_ELEMENT_VALUE_TYPE_S32:
                case PACKET_ELEMENT_VALUE_TYPE_F64:
                case PACKET_ELEMENT_VALUE_TYPE_F32:
                case PACKET_ELEMENT_VALUE_TYPE_B32:
                case PACKET_ELEMENT_VALUE_TYPE_C8:
                b32 data = jd_ArenaStrAppendBin(packet_str, &header->elements[i]->data, packet_type_sizes[header->elements[i]->value_type]);
                break;
                
                case PACKET_ELEMENT_VALUE_TYPE_STRING:
                b32 string_w = jd_ArenaStrAppendCountAndStr(packet_str, header->elements[i]->data.str);
                break;
            }
            
            b32 semic_w = jd_ArenaStrAppendStr(packet_str, semic_s);
        }
        
        b32 c_brac_w = jd_ArenaStrAppendStr(packet_str, c_bracket_s);
        
        header = header->next;
        if (header == packet->last->next) break;
    }
    
    if (!use_passed_arenastr) {
        jd_StrA dup_str = jd_StrDup(arena, packet_str->str);
        jd_ArenaStrRelease(packet_str);
        return dup_str;
    }
    else {
        return packet_str->str;
    }
}

u64 PacketCalcStringLength(jdat_Packet* packet) {
    PacketHeader* header = packet->head;
    u64 calc_count = 0;
    while (header != NULL) {
        calc_count += header->text_size;
        header = header->next;
        if (header == packet->last->next) break;
    }
    return calc_count;
}

jd_StrA o_bracket_s = jd_StrAConst(" {\n");
jd_StrA equals_s = jd_StrAConst(" = ");
jd_StrA semic_s = jd_StrAConst(";\n");
jd_StrA c_bracket_s = jd_StrAConst("}\n");

const jd_StrA element_type_strings[PACKET_ELEMENT_VALUE_TYPE_COUNT] = {
    jd_StrAConst("NULL:"),
    jd_StrAConst("u64:"),
    jd_StrAConst("u32:"),
    jd_StrAConst("s64:"),
    jd_StrAConst("s32:"),
    jd_StrAConst("f64:"),
    jd_StrAConst("f32:"),
    jd_StrAConst("b32:"),
    jd_StrAConst("c8:"),
    jd_StrAConst("string:")
};

b32 PacketHeaderAppendToArenaStr(PacketHeader* header, jd_ArenaStr* arena_str, b32 limit_value_string_len, u64 max_value_string_len) {
    if (header->num_elements == 0) return false;
    b32 at_w = jd_ArenaStrAppendC8(arena_str, '@');
    b32 tag_w = jd_ArenaStrAppendStr(arena_str, header->tag);
    b32 o_brac_w = jd_ArenaStrAppendStr(arena_str, o_bracket_s);
    
    for (u32 i = 0; i < header->num_elements; i++) {
        b32 key_w = jd_ArenaStrAppendStr(arena_str, header->elements[i]->key);
        b32 equal_w = jd_ArenaStrAppendStr(arena_str, equals_s);
        b32 type_s_w = jd_ArenaStrAppendStr(arena_str, element_type_strings[header->elements[i]->value_type]);
        switch (header->elements[i]->value_type) {
            case PACKET_ELEMENT_VALUE_TYPE_U64:
            case PACKET_ELEMENT_VALUE_TYPE_U32:
            case PACKET_ELEMENT_VALUE_TYPE_S64:
            case PACKET_ELEMENT_VALUE_TYPE_S32:
            case PACKET_ELEMENT_VALUE_TYPE_F64:
            case PACKET_ELEMENT_VALUE_TYPE_F32:
            case PACKET_ELEMENT_VALUE_TYPE_B32:
            case PACKET_ELEMENT_VALUE_TYPE_C8:
            b32 data = jd_ArenaStrAppendBin(arena_str, &header->elements[i]->data, packet_type_sizes[header->elements[i]->value_type]);
            break;
            
            case PACKET_ELEMENT_VALUE_TYPE_STRING:
            if (limit_value_string_len) {
                jd_StrA trunc_str = header->elements[i]->data.str;
                trunc_str.count = max_value_string_len;
                b32 string_w = jd_ArenaStrAppendCountAndStr(arena_str, trunc_str);
            }
            
            else {
                b32 string_w = jd_ArenaStrAppendCountAndStr(arena_str, header->elements[i]->data.str);
                // append some sort of ellipses?
            }
            
            break;
        }
        
        b32 semic_w = jd_ArenaStrAppendStr(arena_str, semic_s);
    }
    
    b32 c_brac_w = jd_ArenaStrAppendStr(arena_str, c_bracket_s);
    return true;
}

void PacketJoinToBack(jdat_Packet* to_packet, jdat_Packet* from_packet) {
    if (to_packet->last == NULL) { to_packet->last = from_packet->head; to_packet->head = from_packet->head; }
    else to_packet->last->next = from_packet->head; 
    
    to_packet->last = from_packet->last;
}

b32 PacketCopyToBack(jd_Arena* arena, jdat_Packet* to_packet, jdat_Packet* from_packet) {
    b32 success = true;
    PacketHeader* from_head = from_packet->head;
    PacketHeader* from_last = from_packet->last;
    
    jdat_Packet* from_copy = PacketCreate(arena);
    
    if (from_copy == NULL) {
        success = false;
        return success;
    }
    
    while (from_head != NULL && from_head != from_last->next) {
        PacketHeader* header = PacketHeaderPushBack(from_copy, from_head->tag);
        for (u64 i = 0; i < from_head->num_elements; i++) {
            PacketElement* element = PacketElementPushBack(header, from_head->elements[i]);
        }
        
        from_head = from_head->next;
    }
    
    PacketJoinToBack(to_packet, from_copy);
    return true;
}

PacketHeader* PacketHeaderCopy(jd_Arena* arena, PacketHeader* src) {
    PacketHeader* dst = jd_ArenaAlloc(arena, sizeof(*dst));
    dst->arena = arena;
    dst->tag = jd_StrDup(dst->arena, src->tag);
    dst->num_elements = src->num_elements;
    for (u64 i = 0; i < dst->num_elements; i++) {
        dst->elements[i] = jd_ArenaAlloc(arena, sizeof(PacketElement));
        PacketElement* dst_e = dst->elements[i];
        dst_e->key = jd_StrDup(dst->arena, src->elements[i]->key);
        dst_e->value_type = src->elements[i]->value_type;
        if (dst_e->value_type != PACKET_ELEMENT_VALUE_TYPE_STRING)
            dst_e->data = src->elements[i]->data;
        else 
            dst_e->data.str = jd_StrDup(arena, src->elements[i]->data.str);
    }
    dst->text_size = src->text_size;
    return dst;
}

u64 PacketElementGetU64(PacketElement* packet_element) {
    if (packet_element->value_type != PACKET_ELEMENT_VALUE_TYPE_U64) return 0;
    return packet_element->data.U64;
}

u32 PacketElementGetU32(PacketElement* packet_element) {
    if (packet_element->value_type != PACKET_ELEMENT_VALUE_TYPE_U32) return 0;
    return packet_element->data.U32;
}

s64 PacketElementGetS64(PacketElement* packet_element) {
    if (packet_element->value_type != PACKET_ELEMENT_VALUE_TYPE_S64) return 0;
    return packet_element->data.S64;
}

s32 PacketElementGetS32(PacketElement* packet_element) {
    if (packet_element->value_type != PACKET_ELEMENT_VALUE_TYPE_S32) return 0;
    return packet_element->data.S32;
}

f64 PacketElementGetF64(PacketElement* packet_element) {
    if (packet_element->value_type != PACKET_ELEMENT_VALUE_TYPE_F64) return 0;
    return packet_element->data.F64;
}

f32 PacketElementGetF32(PacketElement* packet_element) {
    if (packet_element->value_type != PACKET_ELEMENT_VALUE_TYPE_F32) return 0.0f;
    return packet_element->data.F32;
}

b32 PacketElementGetB32(PacketElement* packet_element) {
    if (packet_element->value_type != PACKET_ELEMENT_VALUE_TYPE_B32) return false;
    return packet_element->data.B32;
}

c8 PacketElementGetC8(PacketElement* packet_element) {
    if (packet_element->value_type != PACKET_ELEMENT_VALUE_TYPE_C8) return 0;
    return packet_element->data.C8;
}

jd_StrA PacketElementGetString(PacketElement* packet_element) {
    jd_StrA str = {0};
    if (packet_element->value_type != PACKET_ELEMENT_VALUE_TYPE_STRING) return str;
    return packet_element->data.str;
}


