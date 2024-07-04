#ifndef JD_DATABANK_H
#define JD_DATABANK_H

// NOTE(JD): This may well prove to be evil macro magic.
#define jd_Node(type) \
struct { \
struct type* next; \
struct type* prev; \
struct type* first_child; \
struct type* last_child; \
struct type* parent; \
} \

#define jd_ForSLL(i, cond) for (; cond; i = i->next)
#define jd_SLNext(x) x->next
#define jd_SLink(x, y) \
do { \
void* _next = x->next; \
x->next = y; \
x->next->next = _next; \
} while (0) \

#define jd_SLinkClear(x) \
do { \
x->next = 0; \
} while (0) \

#define jd_ForDLLForward(i, cond) jd_ForSLL(i, cond)
#define jd_ForDLLBackward(i, cond) for (; cond; i = i->prev)
#define jd_DLNext(x) jd_SLNext(x)
#define jd_DLPrev(x) x->prev

#define jd_DLinkNext(x, y) \
do { \
void* _next = x->next; \
x->next = y; \
x->next->next = _next; \
x->next->prev = x; \
} while (0) \

#define jd_DLinkPrev(x, y) \
do { \
void* _prev = x->prev; \
x->prev = y; \
x->prev->prev = _prev; \
x->prev->next = x; \
} while (0) \

#define jd_DLinksClear(x) \
do { \
jd_SLinkClear(x); \
x->prev = 0; \
} while (0) \

#define jd_TreeLinkNext(x, y) \
do { \
jd_DLinkNext(x, y); \
y->parent = x->parent; \
if (x->parent->last_child == x) { \
x->parent->last_child = y; \
} \
} while (0) \

#define jd_TreeLinkPrev(x, y) \
do { \
jd_DLinkPrev(x, y); \
y->parent = x->parent; \
if (x->parent->first_child == x) { \
x->parent->first_child = y; \
} \
} while (0) \

#define jd_TreeLinkLastChild(p, c) \
do { \
void* _lc = p->last_child; \
c->parent = p; \
c->prev = _lc; \
c->next = 0; \
if (p->last_child)   { p->last_child->next = c; } \
if (!p->first_child) { p->first_child = c; } \
p->last_child = c; \
} while (0) \

#define jd_TreeLinkFirstChild(p, c) \
do { \
void* _fc = p->first_child; \
c->parent = p; \
c->next = _fc; \
c->prev = 0; \
if (p->first_child) p->first_child->prev = c; \
if (!p->last_child) p->last_child = c; \
p->first_child = c; \
} while (0) \

#define jd_TreeLinksClear(x) \
do { \
if (x->parent) { \
if (x->parent->first_child == x) x->parent->first_child = x->next; \
if (x->parent->last_child == x)  x->parent->last_child = x->prev; \
} \
\
jd_DLinksClear(x); \
x->parent = 0; \
x->last_child = 0; \
x->first_child = 0; \
} while (0) \

#define jd_TreeTraversePreorder(x) \
do { \
if (x->first_child) { \
x = x->first_child; \
break; \
} \
else if (x->next) { \
x = x->next; \
break; \
} \
else { \
while (x != 0 && x->next == 0) { \
x = x->parent; \
} \
\
if (x != 0) \
x = x->next; \
} \
} while (0) \

typedef enum jd_DataType {
    jd_DataType_None = 0,
    jd_DataType_String =  1 << 0,
    jd_DataType_Bin =     1 << 1,
    jd_DataType_u64 =     1 << 2,
    jd_DataType_u32 =     1 << 3,
    jd_DataType_b32 =     1 << 4,
    jd_DataType_c8  =     1 << 5,
    jd_DataType_i64 =     1 << 6,
    jd_DataType_i32 =     1 << 7,
    jd_DataType_f32 =     1 << 8,
    jd_DataType_f64 =     1 << 9,
    jd_DataType_Record =  1 << 10,
    jd_DataType_Root   =  1 << 11,
    jd_DataType_Count
} jd_DataType;

typedef struct jd_Value {
    jd_DataType type;
    union {
        jd_String string;
        jd_View   bin;
        u64 U64;
        u32 U32;
        b8  B32;
        c8  C8;
        i64 I64;
        i32 I32;
        f32 F32;
        f64 F64;
    };
} jd_Value;

typedef struct jd_DataNodeOptions {
    jd_String display;
} jd_DataNodeOptions;

typedef struct jd_DataNode {
    struct jd_DataBank* bank;
    
    jd_RWLock* lock;
    
    jd_String key;
    jd_Value  value;
    
    jd_String display;
    
    u32 slot_taken;
    
    struct jd_DataNode* next_with_same_hash;
    
    jd_Node(jd_DataNode);
} jd_DataNode;

typedef struct jd_DataBank {
    jd_Arena* arena;
    jd_RWLock* lock;
    
    u64 primary_key_index;
    
    jd_DataType disabled_types;
    
    jd_DataNode* primary_key_hash_table;
    u64           primary_key_hash_table_slot_count;
    
    jd_DataNode* root;
} jd_DataBank;

typedef struct jd_DataBankConfig {
    jd_String name;
    jd_DataType disabled_types; // |= types to this flag to disable them
    u64 total_memory_cap;
    u64 primary_key_hash_table_slot_count;
    u64 primary_key_index;
} jd_DataBankConfig;

jd_DataBank*  jd_DataBankCreate(jd_DataBankConfig* config);
jd_DFile*     jd_DataBankSerialize(jd_DataBank* bank);
jd_DataBank*  jd_DataBankDeserialize(jd_File view);

jd_ForceInline jd_DataNode* jd_DataBankGetRoot(jd_DataBank* bank);

jd_DataNode*   jd_DataBankAddRecord(jd_DataNode* parent, jd_String key, jd_DataNodeOptions* options);
jd_DataNode*   jd_DataBankAddRecordWithPK(jd_DataNode* parent, jd_String key, u64 primary_key, jd_DataNodeOptions* options);
jd_DataNode*   jd_DataPointAdd(jd_DataNode* parent, jd_String key, jd_Value value, jd_DataNodeOptions* options);
jd_Value       jd_DataPointGetValue(jd_DataNode* record, jd_String key);

jd_ForceInline jd_Value jd_ValueCastString(jd_String string);
jd_ForceInline jd_Value jd_ValueCastBin(jd_View view);
jd_ForceInline jd_Value jd_ValueCastU64(u64 val);
jd_ForceInline jd_Value jd_ValueCastU32(u32 val);
jd_ForceInline jd_Value jd_ValueCastB32(b32 val);
jd_ForceInline jd_Value jd_ValueCastC8(c8 val);
jd_ForceInline jd_Value jd_ValueCastI64(i64 val);
jd_ForceInline jd_Value jd_ValueCastI32(i32 val);
jd_ForceInline jd_Value jd_ValueCastF32(f32 val);
jd_ForceInline jd_Value jd_ValueCastF64(f64 val);

jd_ForceInline jd_String jd_ValueString(jd_Value v);
jd_ForceInline jd_View   jd_ValueBin(jd_Value v);
jd_ForceInline u64       jd_ValueU64(jd_Value v);
jd_ForceInline u32       jd_ValueU32(jd_Value v);
jd_ForceInline b32       jd_ValueB32(jd_Value v);
jd_ForceInline c8        jd_ValueC8 (jd_Value v);
jd_ForceInline i64       jd_ValueI64(jd_Value v);
jd_ForceInline i32       jd_ValueI32(jd_Value v);
jd_ForceInline f32       jd_ValueF32(jd_Value v);
jd_ForceInline f64       jd_ValueF64(jd_Value v);

#endif