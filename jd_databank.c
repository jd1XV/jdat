// Note: This is basically MurmurHash3, just with a fixed input size.
#include "jd_databank.h"

jd_ForceInline u32 _jd_RotateLeft32(u32 x, u8 r) {
    return (x << r) | (x >> (32 - r));
}

jd_ForceInline u32 _jd_FinalMix32(u32 hash) {
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;
    
    return hash;
}

u32 jd_HashU32toU32(u32 in, u32 seed) {
    u32 hash = seed;
    
    u32 c1 = 0xcc9e2d51;
    u32 c2 = 0x1b873593;
    
    u32 k1 = in;
    if (k1 >> 8 == 0) k1 |= (k1 << 8);
    if (k1 >> 16 == 0) k1 |= (k1 << 16);
    if (k1 >> 24 == 0) k1 |= (k1 << 24);
    
    k1 *= c1;
    k1 = _jd_RotateLeft32(k1, 15);
    k1 *= c2;
    
    hash ^= k1;
    hash = _jd_RotateLeft32(hash, 13); 
    hash = hash * 5 + 0xe6546b64;
    
    hash ^= 4 * (in >> 3);
    
    hash = _jd_FinalMix32(hash);
    return hash;
}

u32 jd_HashStrToU32(jd_String input_str, u32 seed) {
    u32 num_blocks = input_str.count / 4;
    u32 hash = seed;
    
    u32 c1 = 0xcc9e2d51;
    u32 c2 = 0x1b873593;
    
    u32 i = 0;
    
    for (i = 0; i < num_blocks; i++) {
        u32 k1 = *(u32*)(input_str.mem + (i * 4));
        
        k1 *= c1;
        k1 = _jd_RotateLeft32(k1, 15);
        k1 *= c2;
        
        hash ^= k1;
        hash = _jd_RotateLeft32(hash, 13); 
        hash = hash * 5 + 0xe6546b64;
    }
    
    u32 k1 = 0;
    
    switch (input_str.count & 3) {
        case 3: k1 ^= input_str.mem[i + 2] << 16;
        case 2: k1 ^= input_str.mem[1 + 1] << 8;
        case 1: k1 ^= input_str.mem[i];
        k1 *= c1; 
        k1 = _jd_RotateLeft32(k1, 15); 
        k1 *= c2; 
        hash ^= k1;
    }
    
    hash ^= input_str.count;
    hash = _jd_FinalMix32(hash);
    
    return hash;
}

u32 jd_HashU64toU32(u64 val, u32 seed) {
    jd_String val_as_string = {
        .mem = (u8*)&val,
        .count = sizeof(val)
    };
    
    return jd_HashStrToU32(val_as_string, seed);
}
jd_DataBank* jd_DataBankCreate(jd_DataBankConfig* config) {
    if (!config) {
        jd_LogError("No config provided to DataBank initilization", jd_Error_APIMisuse, jd_Error_Fatal);
        return 0;
    }
    
    jd_Arena* arena = jd_ArenaCreate(config->total_memory_cap, 0);
    jd_DataBank* db = jd_ArenaAlloc(arena, sizeof(*db));
    db->arena = arena;
    db->primary_key_index = config->primary_key_index;
    db->lock = jd_RWLockCreate(arena);
    db->disabled_types = config->disabled_types;
    db->primary_key_hash_table_slot_count = (config->primary_key_hash_table_slot_count > 0) ? config->primary_key_hash_table_slot_count : KILOBYTES(128);
    db->primary_key_hash_table = jd_ArenaAlloc(arena, sizeof(jd_DataNode) * db->primary_key_hash_table_slot_count);
    db->root = jd_ArenaAlloc(arena, sizeof(*db->root));
    db->root->bank = db;
    db->root->value.type = jd_DataType_Root;
    db->root->value.U64 = -1;
    return db;
}

jd_DataNode* jd_DataBankGetRoot(jd_DataBank* bank) {
    jd_RWLockGet(bank->lock, jd_RWLock_Read);
    jd_DataNode* root = bank->root;
    jd_RWLockRelease(bank->lock, jd_RWLock_Read);
    
    return root;
}

u64 jd_DataBankGetPrimaryKey(jd_DataBank* bank) {
    jd_RWLockGet(bank->lock, jd_RWLock_Write);
    u64 key = bank->primary_key_index++;
    jd_RWLockRelease(bank->lock, jd_RWLock_Write);
    return key;
}

u64 jd_DataBankGetHashTableSlot(jd_DataBank* bank, u64 primary_key) {
    static const u32 seed = 963489887;
    u32 hash = jd_HashU64toU32(primary_key, seed);
    return (hash & (bank->primary_key_hash_table_slot_count - 1));
}

jd_DataNode* jd_DataBankGetRecordWithID(jd_DataBank* bank, u64 primary_key) {
    jd_RWLockGet(bank->lock, jd_RWLock_Read);
    u64 slot = jd_DataBankGetHashTableSlot(bank, primary_key);
    jd_DataNode* n = &bank->primary_key_hash_table[slot];
    while (n != 0) {
        if (n->value.type == jd_DataType_Record && n->value.U64 == primary_key) {
            break;
        }
        
        n = n->next_with_same_hash;
    }
    jd_RWLockRelease(bank->lock, jd_RWLock_Read);
    
    return n;
}

jd_DataNode* jd_DataBankAddRecordWithPK(jd_DataNode* parent, jd_String key, u64 primary_key, jd_DataNodeOptions* options) {
    if (!parent) {
        jd_LogError("No and/or null parent specified. To create a top level record, specify the DataBank root as parent. DataBank root can be accessed (thread-safe-ly) by using jd_DataBankGetRoot(my_data_bank).", jd_Error_APIMisuse, jd_Error_Fatal);
    }
    
    jd_DataBank* db = parent->bank;
    
    if (!db) {
        jd_LogError("Specified parent does not belong to a DataBank.", jd_Error_APIMisuse, jd_Error_Fatal);
    }
    
    jd_RWLockGet(db->lock, jd_RWLock_Write);
    
    u32 slot = jd_DataBankGetHashTableSlot(db, primary_key);
    jd_DataNode* n = &db->primary_key_hash_table[slot];
    if (n->value.type != jd_DataType_None) {
        while (n->next_with_same_hash != 0) {
            n = n->next_with_same_hash;
        }
        
        n->next_with_same_hash = jd_ArenaAlloc(db->arena, sizeof(*n->next_with_same_hash));
        n = n->next_with_same_hash;
    }
    
    if (options && options->display.count > 0) {
        n->display = jd_StringPush(db->arena, options->display);
    }
    
    n->lock = jd_RWLockCreate(db->arena);
    n->key = jd_StringPush(db->arena, key);
    n->value.U64 = primary_key;
    n->value.type = jd_DataType_Record;
    n->bank = db;
    jd_TreeLinkLastChild(parent, n);
    
    jd_RWLockRelease(db->lock, jd_RWLock_Write);
    
    return n;
}

jd_DataNode* jd_DataBankAddRecord(jd_DataNode* parent, jd_String key, jd_DataNodeOptions* options) {
    if (!parent) {
        jd_LogError("No and/or null parent specified. To create a top level record, specify the DataBank root as parent. DataBank root can be accessed (thread-safe-ly) by using jd_DataBankGetRoot(my_data_bank).", jd_Error_APIMisuse, jd_Error_Fatal);
    }
    
    jd_DataBank* db = parent->bank;
    
    if (!db) {
        jd_LogError("Specified parent does not belong to a DataBank.", jd_Error_APIMisuse, jd_Error_Fatal);
    }
    
    u64 pk = jd_DataBankGetPrimaryKey(db);
    jd_DataNode* n = jd_DataBankAddRecordWithPK(parent, key, pk, options);
    
    return n;
}

jd_DataNode* jd_DataPointAdd(jd_DataNode* record, jd_String key, jd_Value value, jd_DataNodeOptions* options) {
    if (value.type == jd_DataType_None) {
        jd_LogError("No data type specified.", jd_Error_APIMisuse, jd_Error_Fatal);
    }
    
    else if (value.type == jd_DataType_Record) {
        jd_LogError("Value specified is a record. To add a sub-record, use jd_DataBankAddRecord", jd_Error_APIMisuse, jd_Error_Fatal);
    }
    
    if (!record) {
        jd_LogError("Null record specified. To create a top level record, specify the DataBank root as parent. DataBank root can be accessed (thread-safe-ly) by using jd_DataBankGetRoot(my_data_bank).", jd_Error_APIMisuse, jd_Error_Fatal);
    }
    
    jd_DataBank* bank = record->bank;
    
    if (!bank) {
        jd_LogError("Specified record does not belong to a DataBank", jd_Error_APIMisuse, jd_Error_Fatal);
    }
    
    if ((bank->disabled_types & value.type) != 0) {
        jd_LogError("Specified value type is disabled for this bank.", jd_Error_APIMisuse, jd_Error_Critical);
        return 0;
    }
    
    if (record->value.type != jd_DataType_Record) {
        jd_LogError("Node passed to 'record' is not of type jd_DataType_Record.", jd_Error_APIMisuse, jd_Error_Fatal);
    }
    
    jd_RWLockGet(record->lock, jd_RWLock_Write);
    
    jd_DataNode* n = jd_ArenaAlloc(bank->arena, sizeof(*n));
    n->key = jd_StringPush(bank->arena, key);
    n->value.type = value.type;
    
    if (options && options->display.count > 0) {
        n->display = jd_StringPush(bank->arena, options->display);
    }
    
    
    switch (value.type) {
        case jd_DataType_String: {
            n->value.string = jd_StringPush(bank->arena, value.string);
            break;
        }
        
        case jd_DataType_Bin: {
            n->value.bin = jd_StringPush(bank->arena, value.bin);
            break;
        }
        
        case jd_DataType_u64:
        case jd_DataType_u32:
        case jd_DataType_b32:
        case jd_DataType_c8:
        case jd_DataType_i64:
        case jd_DataType_i32:
        case jd_DataType_f32: 
        case jd_DataType_f64: {
            n->value = value;
            break;
        }
        
    }
    
    jd_TreeLinkLastChild(record, n);
    
    jd_RWLockRelease(record->lock, jd_RWLock_Write);
    
    return n;
}

jd_Value jd_DataPointGetValue(jd_DataNode* record, jd_String key) {
    if (!record) {
        jd_LogError("No/null record specified.", jd_Error_APIMisuse, jd_Error_Fatal);
    }
    
    if (record->value.type != jd_DataType_Record) {
        jd_LogError("DataNode is not a record.", jd_Error_APIMisuse, jd_Error_Fatal);
    }
    
    jd_Value v = {0};
    
    jd_RWLockGet(record->lock, jd_RWLock_Read);
    
    jd_DataNode* n = record->first_child;
    jd_ForDLLForward(n, n != 0) {
        if (jd_StringMatch(key, n->key)) {
            v = n->value;
            break;
        }
    }
    
    jd_RWLockRelease(record->lock, jd_RWLock_Read);
    
    return v;
}

jd_Value jd_ValueCastString(jd_String string) {
    jd_Value v = {0};
    v.type = jd_DataType_String;
    v.string = string;
    return v;
}

jd_Value jd_ValueCastBin(jd_View view) {
    jd_Value v = {0};
    v.type = jd_DataType_Bin;
    v.bin = view;
    return v;
}

jd_Value jd_ValueCastU64(u64 val) {
    jd_Value v = {0};
    v.type = jd_DataType_u64;
    v.U64 = val;
    return v;
}

jd_Value jd_ValueCastU32(u32 val) {
    jd_Value v = {0};
    v.type = jd_DataType_u32;
    v.U32 = val;
    return v;
}

jd_Value jd_ValueCastB32(b32 val) {
    jd_Value v = {0};
    v.type = jd_DataType_b32;
    v.B32 = val;
    return v;
}

jd_Value jd_ValueCastC8(c8 val) {
    jd_Value v = {0};
    v.type = jd_DataType_c8;
    v.C8 = val;
    return v;
}

jd_Value jd_ValueCastI64(i64 val) {
    jd_Value v = {0};
    v.type = jd_DataType_i64;
    v.I64 = val;
    return v;
}

jd_Value jd_ValueCastI32(i32 val) {
    jd_Value v = {0};
    v.type = jd_DataType_i32;
    v.I32 = val;
    return v;
}

jd_Value jd_ValueCastF32(f32 val) {
    jd_Value v = {0};
    v.type = jd_DataType_f32;
    v.F32 = val;
    return v;
}

jd_Value jd_ValueCastF64(f64 val) {
    jd_Value v = {0};
    v.type = jd_DataType_f64;
    v.F64 = val;
    return v;
}

#define jd_ValueCheckAssign(v)  

jd_String jd_ValueString(jd_Value v) {
    if (v.type == jd_DataType_String)
        return v.string;
    return (jd_String){0};
}

jd_View jd_ValueBin(jd_Value v) {
    if (v.type == jd_DataType_Bin)
        return v.bin;
    return (jd_View){0};
}

u64 jd_ValueU64(jd_Value v) {
    if (v.type == jd_DataType_u64 || jd_DataType_Record)
        return v.U64;
    return 0;
}

u32 jd_ValueU32(jd_Value v) {
    if (v.type == jd_DataType_u32)
        return v.U32;
    return 0;
}

b32 jd_ValueB32(jd_Value v) {
    if (v.type == jd_DataType_b32)
        return v.B32;
    return 0;
}

c8 jd_ValueC8(jd_Value v) {
    if (v.type == jd_DataType_c8)
        return v.C8;
    return 0;
}

i64 jd_ValueI64(jd_Value v) {
    if (v.type == jd_DataType_i64)
        return v.I64;
    return 0;
}

i32 jd_ValueI32(jd_Value v) {
    if (v.type == jd_DataType_i32)
        return v.I32;
    return 0;
}

f32 jd_ValueF32(jd_Value v) {
    if (v.type == jd_DataType_f32)
        return v.F32;
    return 0;
}

f64 jd_ValueF64(jd_Value v) {
    if (v.type == jd_DataType_f64)
        return v.F64;
    return 0;
}

const static u32 jd_databank_magic_number = 0x2E6D6150;
const static u64 jd_databank_root_pk = -1;

jd_DataBank* jd_DataBankDeserialize(jd_File file) {
    typedef enum p_state {
        p_need_type,
        p_need_key_count,
        p_need_key,
        p_need_display_count,
        p_need_parent,
        p_need_data,
        p_build
    } p_state;
    
    typedef struct p_data {
        jd_String key;
        jd_Value value;
        u64 key_count;
        u64 parent;
        jd_String display;
    } p_data;
    
    u64 index = 0;
    u32 magic = 0;
    u64 pk_index = 0;
    u64 hash_table_slot_count = 0;
    jd_DataType disabled_types = 0;
    
    b32 read_success = 0;
    
    read_success = jd_FileReadU32(file, &index, &magic);
    
    if (magic != jd_databank_magic_number) {
        jd_LogError("File is not a DataBank", jd_Error_BadInput, jd_Error_Critical);
        return 0;
    }
    
    read_success = jd_FileReadU64(file, &index, &pk_index);
    read_success = jd_FileReadU64(file, &index, &hash_table_slot_count);
    read_success = jd_FileReadU32(file, &index, &(u32)disabled_types);
    
    if (!read_success) {
        jd_LogError("File is not a DataBank, header incomplete or missing.", jd_Error_BadInput, jd_Error_Critical);
        return 0;
    }
    
    jd_DataBankConfig cfg = {
        .disabled_types = disabled_types,
        .primary_key_hash_table_slot_count = hash_table_slot_count,
        .primary_key_index = pk_index
    };
    
    jd_DataBank* bank = jd_DataBankCreate(&cfg);
    bank->primary_key_index = pk_index;
    
    p_state state = 0;
    p_data data = {0}; 
    
    while (read_success) {
        switch (state) {
            case p_need_type: {
                read_success = jd_FileReadU32(file, &index, (u32*)&data.value.type);
                state = p_need_key_count;
                break;
            }
            
            case p_need_key_count: {
                read_success = jd_FileReadU64(file, &index, &data.key_count);
                state = p_need_key;
                break;
            }
            
            case p_need_key: {
                read_success = jd_FileReadString(file, &index, data.key_count, &data.key);
                if (!read_success) return bank;
                state = p_need_display_count;
                break;
            }
            
            case p_need_display_count: {
                read_success = jd_FileReadU64(file, &index, &data.display.count);
                if (data.display.count) {
                    jd_FileReadString(file, &index, data.display.count, &data.display);
                }
                
                state = p_need_parent;
                break;
            }
            
            case p_need_parent: {
                read_success = jd_FileReadU64(file, &index, &data.parent);
                state = p_need_data;
                break;
            }
            
            case p_need_data: {
                switch (data.value.type) {
                    case jd_DataType_String: {
                        read_success = jd_FileReadU64(file, &index, &data.value.string.count);
                        read_success = jd_FileReadString(file, &index, data.value.string.count, &data.value.string);
                        break;
                    }
                    case jd_DataType_Bin: {
                        read_success = jd_FileReadU64(file, &index, &data.value.bin.size);
                        read_success = jd_FileReadString(file, &index, data.value.bin.size, &data.value.bin);
                        break;
                    }
                    
                    case jd_DataType_u64:
                    case jd_DataType_Record: {
                        read_success = jd_FileReadU64(file, &index, &data.value.U64);
                        break;
                    }
                    
                    case jd_DataType_u32: {
                        read_success = jd_FileReadU32(file, &index, &data.value.U32);
                        break;
                    }
                    
                    case jd_DataType_c8: {
                        read_success = jd_FileReadC8(file, &index, &data.value.C8);
                        break;
                    }
                    
                    case jd_DataType_i64: {
                        read_success = jd_FileReadI64(file, &index, &data.value.I64);
                        break;
                    }
                    
                    case jd_DataType_i32: {
                        read_success = jd_FileReadI32(file, &index, &data.value.U32);
                        break;
                    }
                    
                    case jd_DataType_f32: {
                        read_success = jd_FileReadF32(file, &index, &data.value.F32);
                        break;
                    }
                    
                    case jd_DataType_f64: {
                        read_success = jd_FileReadF64(file, &index, &data.value.F64);
                        break;
                    }
                    
                    case jd_DataType_None:
                    case jd_DataType_Root: {
                        jd_LogError("Invalid type specified in Databank file!", jd_Error_APIMisuse, jd_Error_Fatal);
                        return bank;
                    }
                }
                
                state = p_build;
                break;
            }
            
            case p_build: {
                jd_DataNode* p = 0;
                if (data.parent == jd_databank_root_pk) {
                    p = jd_DataBankGetRoot(bank);
                } else {
                    p = jd_DataBankGetRecordWithID(bank, data.parent);
                }
                
                if (!p) {
                    jd_LogError("Invalid parent specified in DataBank file.", jd_Error_BadInput, jd_Error_Fatal);
                    read_success = false;
                    break;
                }
                
                jd_DataNodeOptions opt = {
                    .display = data.display
                };
                
                if (data.value.type == jd_DataType_Record) {
                    jd_DataNode* n = jd_DataBankAddRecordWithPK(p, data.key, data.value.U64, &opt);
                } else {
                    jd_DataNode* n = jd_DataPointAdd(p, data.key, data.value, &opt);
                }
                
                jd_MemSet(&data, 0, sizeof(data));
                state = p_need_type;
                break;
            }
        }
    }
    
    return bank;
    
    return 0;
}

jd_DFile* jd_DataBankSerialize(jd_DataBank* bank) {
    if (!bank) {
        jd_LogError("No databank specified!", jd_Error_APIMisuse, jd_Error_Fatal);
    }
    
    jd_DataNode* root = jd_DataBankGetRoot(bank);
    jd_DataNode* n = root->first_child;
    
    jd_RWLockGet(bank->lock, jd_RWLock_Read);
    
    jd_DFile* out = jd_DFileCreate(bank->arena->pos);
    jd_DFileAppend(out, &jd_databank_magic_number);
    jd_DFileAppend(out, &bank->primary_key_index);
    jd_DFileAppend(out, &bank->primary_key_hash_table_slot_count);
    jd_DFileAppend(out, &bank->disabled_types);
    
    while (n != 0) {
        jd_Value v = n->value;
        jd_DFileAppend(out, &v.type);
        jd_DFileAppend(out, &n->key.count);
        jd_DFileAppendSized(out, n->key.count, n->key.mem);
        jd_DFileAppend(out, &n->display.count);
        if (n->display.count > 0) {
            jd_DFileAppendSized(out, n->display.count, n->display.mem);
        }
        
        if (n->parent == root) {
            jd_DFileAppend(out, &jd_databank_root_pk);
        } else {
            jd_DFileAppend(out, &n->parent->value.U64);
        }
        
        switch (v.type) {
            case jd_DataType_String: {
                jd_String str = jd_ValueString(v);
                jd_DFileAppend(out, &str.count);
                jd_DFileAppendSized(out, str.count, str.mem);
                break;
            }
            case jd_DataType_Bin: {
                jd_View bin = jd_ValueBin(v);
                jd_DFileAppend(out, &bin.size);
                jd_DFileAppendSized(out, bin.size, bin.mem);
                break;
            }
            
            case jd_DataType_u64:
            case jd_DataType_Record: {
                u64 u = jd_ValueU64(v);
                jd_DFileAppend(out, &u);
                break;
            }
            
            case jd_DataType_u32: {
                u32 u = jd_ValueU32(v);
                jd_DFileAppend(out, &u);
                break;
            }
            
            case jd_DataType_c8: {
                c8 c = jd_ValueC8(v);
                jd_DFileAppend(out, &c);
                break;
            }
            
            case jd_DataType_i64: {
                i64 i = jd_ValueI64(v);
                jd_DFileAppend(out, &i);
                break;
            }
            
            case jd_DataType_i32: {
                i32 i = jd_ValueI32(v);
                jd_DFileAppend(out, &i);
                break;
            }
            
            case jd_DataType_f32: {
                f32 f = jd_ValueF32(v);
                jd_DFileAppend(out, &f);
                break;
            }
            
            case jd_DataType_f64: {
                f64 f = jd_ValueF64(v);
                jd_DFileAppend(out, &f);
                break;
            }
            
            case jd_DataType_None:
            case jd_DataType_Root: {
                jd_LogError("DataNode is untyped or a misplaced root!", jd_Error_APIMisuse, jd_Error_Fatal);
                jd_DFileRelease(out);
                return 0;
            }
        }
        jd_TreeTraversePreorder(n);
    }
    jd_RWLockRelease(bank->lock, jd_RWLock_Read);
    
    return out;
}