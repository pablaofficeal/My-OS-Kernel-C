#include "acpi_aml.h"
#include "acpi.h"
#include "acpi_ec.h"
#include "../../kernel/diagnostics/klog.h"
#include "../../lib/string.h"
#include <stddef.h>

#define AML_ZERO_OP      0x00U
#define AML_ONE_OP       0x01U
#define AML_BYTE_PREFIX  0x0AU
#define AML_WORD_PREFIX  0x0BU
#define AML_DWORD_PREFIX 0x0CU
#define AML_QWORD_PREFIX 0x0EU
#define AML_METHOD_OP    0x14U
#define AML_LOCAL0_OP    0x60U
#define AML_ARG0_OP      0x68U
#define AML_STORE_OP     0x70U
#define AML_L_EQUAL_OP   0x93U
#define AML_IF_OP        0xA0U
#define AML_LOAD_OP      0x5CU
#define AML_STALL_OP     0x73U
#define AML_RETURN_OP    0xA4U
#define AML_INDEX_OP     0x88U
#define AML_EXTOP        0x5BU
#define AML_FIELD_OP     0x81U
#define AML_MUTEX_OP     0x01U
#define AML_ACQUIRE_OP   0x23U
#define AML_RELEASE_OP   0x27U

enum aml_obj_type {
    AML_OBJ_NONE = 0,
    AML_OBJ_INT,
    AML_OBJ_BUFFER,
};

struct aml_obj {
    enum aml_obj_type type;
    uint64_t integer;
    uint8_t buffer[16];
    uint32_t buffer_len;
};

struct aml_ctx {
    const uint8_t *ip;
    const uint8_t *end;
    struct aml_obj locals[8];
    struct aml_obj args[7];
    bool returned;
    uint64_t return_value;
};

struct aml_field {
    char name[5];
    uint8_t offset;
    uint8_t width;
};

static struct aml_field aml_fields[32];
static uint32_t aml_field_count;

static const uint8_t *aml_dsdt_body(uint32_t *length){
    const struct acpi_table_header *dsdt=acpi_get_dsdt();
    if(!dsdt || dsdt->length<=sizeof(struct acpi_table_header))
        return NULL;
    *length=dsdt->length-(uint32_t)sizeof(struct acpi_table_header);
    return (const uint8_t *)dsdt+sizeof(struct acpi_table_header);
}

static bool aml_name_matches(const uint8_t *name, const char *expected){
    if(!name || !expected)
        return false;
    for(int i=0;i<4;i++){
        char c=expected[i] ? expected[i] : '_';
        char n=(char)name[i];
        if(n=='_' && c=='_')
            continue;
        if(n!=c)
            return false;
    }
    return true;
}

static bool aml_parent_hint(const uint8_t *dsdt, uint32_t dsdt_len, const uint8_t *method){
    if(!dsdt || method<dsdt || (uint32_t)(method-dsdt)>dsdt_len)
        return false;
    uint32_t window=(uint32_t)(method-dsdt);
    if(window>4096U)
        window=4096U;
    const uint8_t *start=method-window;
    for(const uint8_t *p=start;p+4<method;p++){
        if(aml_name_matches(p, "ATKD") || aml_name_matches(p, "ASUS")
           || aml_name_matches(p, "WMNB") || aml_name_matches(p, "WMI"))
            return true;
    }
    return false;
}

static const uint8_t *aml_skip_term_arg(const uint8_t *ip, const uint8_t *end);

static const uint8_t *aml_skip_name_string(const uint8_t *ip, const uint8_t *end){
    if(ip>=end)
        return end;
    if(*ip=='\\'){
        ip++;
        while(ip+4<=end){
            ip+=4;
            if(ip<end && *ip=='.')
                continue;
            break;
        }
        return ip;
    }
    if(*ip=='.'){
        ip++;
        if(ip+8<=end)
            return ip+8;
        return end;
    }
    if(*ip=='^'){
        ip++;
        return aml_skip_name_string(ip, end);
    }
    if(*ip=='/'){
        ip++;
        if(ip>=end)
            return end;
        uint8_t count=*ip++;
        if(ip+((uint32_t)count*4U)>end)
            return end;
        return ip+((uint32_t)count*4U);
    }
    if(ip+4<=end)
        return ip+4;
    return end;
}

static const uint8_t *aml_skip_package(const uint8_t *ip, const uint8_t *end){
    if(ip>=end)
        return end;
    uint8_t lead=*ip++;
    uint32_t count=0;
    if(lead<=0x0FU){
        count=lead;
    } else if(lead==0x11U || lead==0x12U){
        if(ip>=end)
            return end;
        count=*ip++;
        if(count==0xFFU){
            if(ip+2> end)
                return end;
            count=(uint32_t)ip[0]|((uint32_t)ip[1]<<8);
            ip+=2;
        }
    } else {
        return ip;
    }
    for(uint32_t i=0;i<count;i++){
        ip=aml_skip_term_arg(ip, end);
        if(ip>=end)
            break;
    }
    return ip;
}

static const uint8_t *aml_skip_term_arg(const uint8_t *ip, const uint8_t *end){
    if(ip>=end)
        return end;
    uint8_t op=*ip++;
    switch(op){
    case AML_ZERO_OP:
    case AML_ONE_OP:
        return ip;
    case AML_BYTE_PREFIX:
        return ip+1<=end ? ip+1 : end;
    case AML_WORD_PREFIX:
        return ip+2<=end ? ip+2 : end;
    case AML_DWORD_PREFIX:
        return ip+4<=end ? ip+4 : end;
    case AML_QWORD_PREFIX:
        return ip+8<=end ? ip+8 : end;
    case AML_LOCAL0_OP: case AML_LOCAL0_OP+1: case AML_LOCAL0_OP+2: case AML_LOCAL0_OP+3:
    case AML_LOCAL0_OP+4: case AML_LOCAL0_OP+5: case AML_LOCAL0_OP+6: case AML_LOCAL0_OP+7:
    case AML_ARG0_OP: case AML_ARG0_OP+1: case AML_ARG0_OP+2: case AML_ARG0_OP+3:
    case AML_ARG0_OP+4: case AML_ARG0_OP+5: case AML_ARG0_OP+6:
        return ip;
    case AML_RETURN_OP:
        return aml_skip_term_arg(ip, end);
    case AML_IF_OP:
        ip=aml_skip_term_arg(ip, end);
        return aml_skip_term_arg(ip, end);
    case AML_STORE_OP:
        ip=aml_skip_term_arg(ip, end);
        return aml_skip_term_arg(ip, end);
    case AML_L_EQUAL_OP:
        ip=aml_skip_term_arg(ip, end);
        return aml_skip_term_arg(ip, end);
    case AML_LOAD_OP:
        ip=aml_skip_term_arg(ip, end);
        return aml_skip_term_arg(ip, end);
    case AML_STALL_OP:
        return aml_skip_term_arg(ip, end);
    case AML_INDEX_OP:
        ip=aml_skip_term_arg(ip, end);
        ip=aml_skip_term_arg(ip, end);
        return aml_skip_term_arg(ip, end);
    case 0x11U: case 0x12U:
        return aml_skip_package(ip-1, end);
    default:
        if(op>=0x11U && op<=0x1FU)
            return aml_skip_package(ip-1, end);
        return ip;
    }
}

static const uint8_t *aml_parse_field_region(const uint8_t *ip, const uint8_t *end){
    if(ip>=end || *ip!=AML_EXTOP || ip+1>=end || ip[1]!=AML_FIELD_OP)
        return NULL;
    ip+=2;
    ip=aml_skip_name_string(ip, end);
    if(ip>=end)
        return NULL;
    uint8_t flags=*ip++;
    (void)flags;
    aml_field_count=0;
    while(ip<end){
        uint8_t prefix=*ip;
        if(prefix==0x00U)
            break;
        if(prefix>0x3FU){
            ip++;
            continue;
        }
        char name[5]={(char)prefix, '\0', '\0', '\0', '\0'};
        ip++;
        if(ip>=end)
            break;
        uint8_t offset=*ip++;
        if(ip>=end)
            break;
        uint8_t width=*ip++;
        if(aml_field_count<32U){
            aml_fields[aml_field_count].name[0]=name[0];
            aml_fields[aml_field_count].name[1]='\0';
            aml_fields[aml_field_count].offset=offset;
            aml_fields[aml_field_count].width=width;
            aml_field_count++;
        }
    }
    if(ip<end && *ip==0x00U)
        ip++;
    return ip;
}

bool acpi_aml_find_method(const char *parent_name, const char *method_name,
                          struct acpi_aml_method *out){
    if(!out || !method_name)
        return false;
    uint32_t dsdt_len=0;
    const uint8_t *body=aml_dsdt_body(&dsdt_len);
    if(!body || dsdt_len<8U)
        return false;

    for(uint32_t offset=0; offset+6U<dsdt_len; offset++){
        if(body[offset]!=AML_METHOD_OP)
            continue;
        const uint8_t *name=body+offset+1;
        if(!aml_name_matches(name, method_name))
            continue;
        uint8_t flags=name[4];
        uint8_t arg_count=flags & 0x07U;
        const uint8_t *term=body+offset+6;
        if(parent_name && parent_name[0]!='\0'){
            if(!aml_parent_hint(body, dsdt_len, body+offset))
                continue;
        }
        out->bytecode=term;
        out->bytecode_length=dsdt_len-offset-6U;
        out->arg_count=arg_count;
        klogf(KLOG_DEBUG, "acpi-aml: found Method(%.4s) args=%u at DSDT+0x%x",
              name, arg_count, (unsigned)(offset+sizeof(struct acpi_table_header)));
        return true;
    }
    return false;
}

static bool aml_eval_obj(struct aml_ctx *ctx, struct aml_obj *out);
static bool aml_eval_target_store(struct aml_ctx *ctx, const uint8_t **ip, struct aml_obj *value);

static bool aml_read_field(const char *name, uint64_t *value){
    for(uint32_t i=0;i<aml_field_count;i++){
        if(aml_fields[i].name[0]!=name[0])
            continue;
        uint8_t raw=0;
        if(!acpi_ec_read(aml_fields[i].offset, &raw))
            return false;
        if(value)
            *value=raw;
        return true;
    }
    return false;
}

static bool aml_write_field(const char *name, uint64_t value){
    for(uint32_t i=0;i<aml_field_count;i++){
        if(aml_fields[i].name[0]!=name[0])
            continue;
        return acpi_ec_write(aml_fields[i].offset, (uint8_t)value);
    }
    return false;
}

static bool aml_eval_simple_name(struct aml_ctx *ctx, uint8_t op, struct aml_obj *out){
    if(op>=AML_LOCAL0_OP && op<=AML_LOCAL0_OP+7){
        *out=ctx->locals[op-AML_LOCAL0_OP];
        return true;
    }
    if(op>=AML_ARG0_OP && op<=AML_ARG0_OP+6){
        *out=ctx->args[op-AML_ARG0_OP];
        return true;
    }
  return false;
}

static bool aml_eval_obj(struct aml_ctx *ctx, struct aml_obj *out){
    if(!ctx || !out || ctx->ip>=ctx->end)
        return false;
    memset(out, 0, sizeof(*out));
    uint8_t op=*ctx->ip++;
    switch(op){
    case AML_ZERO_OP:
        out->type=AML_OBJ_INT;
        out->integer=0;
        return true;
    case AML_ONE_OP:
        out->type=AML_OBJ_INT;
        out->integer=1;
        return true;
    case AML_BYTE_PREFIX:
        if(ctx->ip>=ctx->end)
            return false;
        out->type=AML_OBJ_INT;
        out->integer=*ctx->ip++;
        return true;
    case AML_WORD_PREFIX:
        if(ctx->ip+2>ctx->end)
            return false;
        out->type=AML_OBJ_INT;
        out->integer=(uint64_t)ctx->ip[0]|((uint64_t)ctx->ip[1]<<8);
        ctx->ip+=2;
        return true;
    case AML_DWORD_PREFIX:
        if(ctx->ip+4>ctx->end)
            return false;
        out->type=AML_OBJ_INT;
        out->integer=(uint64_t)ctx->ip[0]
            |((uint64_t)ctx->ip[1]<<8)
            |((uint64_t)ctx->ip[2]<<16)
            |((uint64_t)ctx->ip[3]<<24);
        ctx->ip+=4;
        return true;
    case AML_QWORD_PREFIX:
        if(ctx->ip+8>ctx->end)
            return false;
        out->type=AML_OBJ_INT;
        out->integer=0;
        for(int i=0;i<8;i++)
            out->integer|=(uint64_t)ctx->ip[i]<<(8*i);
        ctx->ip+=8;
        return true;
    case AML_RETURN_OP:{
        struct aml_obj value;
        if(!aml_eval_obj(ctx, &value))
            return false;
        ctx->returned=true;
        ctx->return_value=value.integer;
        return true;
    }
    case AML_IF_OP:{
        struct aml_obj pred;
        if(!aml_eval_obj(ctx, &pred))
            return false;
        const uint8_t *save=ctx->ip;
        if(pred.integer){
            if(!aml_eval_obj(ctx, out))
                return false;
        } else {
            ctx->ip=aml_skip_term_arg(save, ctx->end);
            out->type=AML_OBJ_NONE;
        }
        return true;
    }
    case AML_L_EQUAL_OP:{
        struct aml_obj lhs, rhs;
        if(!aml_eval_obj(ctx, &lhs) || !aml_eval_obj(ctx, &rhs))
            return false;
        out->type=AML_OBJ_INT;
        out->integer=(lhs.integer==rhs.integer) ? 1U : 0U;
        return true;
    }
    case AML_STALL_OP:{
        struct aml_obj usec;
        if(!aml_eval_obj(ctx, &usec))
            return false;
        for(volatile uint64_t i=0;i<usec.integer*100U;i++)
            __asm__ volatile("pause");
        out->type=AML_OBJ_INT;
        out->integer=0;
        return true;
    }
    case AML_LOAD_OP:{
        struct aml_obj field_ref;
        if(!aml_eval_obj(ctx, &field_ref))
            return false;
        const uint8_t *name=ctx->ip;
        ctx->ip=aml_skip_name_string(ctx->ip, ctx->end);
        char field_name[2]={(char)name[0], '\0'};
        uint64_t value=0;
        if(!aml_read_field(field_name, &value))
            value=0;
        out->type=AML_OBJ_INT;
        out->integer=value;
        return true;
    }
    case AML_STORE_OP:{
        struct aml_obj value;
        if(!aml_eval_obj(ctx, &value))
            return false;
        if(!aml_eval_target_store(ctx, &ctx->ip, &value))
            return false;
        *out=value;
        return true;
    }
    default:
        if(op>=AML_LOCAL0_OP && op<=AML_ARG0_OP+6)
            return aml_eval_simple_name(ctx, op, out);
        if(op==AML_EXTOP && ctx->ip<ctx->end){
            uint8_t ext=*ctx->ip++;
            if(ext==AML_FIELD_OP){
                const uint8_t *after=aml_parse_field_region(ctx->ip-2, ctx->end);
                if(after){
                    ctx->ip=after;
                    out->type=AML_OBJ_INT;
                    out->integer=0;
                    return true;
                }
            }
        }
        return false;
    }
}

static bool aml_eval_target_store(struct aml_ctx *ctx, const uint8_t **ip, struct aml_obj *value){
    if(!ctx || !ip || !*ip || !value)
        return false;
    uint8_t op=**ip;
    if(op>=AML_LOCAL0_OP && op<=AML_LOCAL0_OP+7){
        (*ip)++;
        ctx->locals[op-AML_LOCAL0_OP]=*value;
        return true;
    }
    if(op>=AML_ARG0_OP && op<=AML_ARG0_OP+6){
        (*ip)++;
        ctx->args[op-AML_ARG0_OP]=*value;
        return true;
    }
    if(op>=0x20U && op<=0x3FU){
        char field_name[2]={(char)op, '\0'};
        (*ip)++;
        return aml_write_field(field_name, value->integer);
    }
    return false;
}

bool acpi_aml_evaluate_method(const struct acpi_aml_method *method,
                              const uint64_t args[7], uint8_t arg_count,
                              uint64_t *retval){
    if(!method || !method->bytecode || method->bytecode_length==0)
        return false;
    if(!acpi_ec_init())
        klog(KLOG_WARN, "acpi-aml: EC unavailable, method may fail");

    struct aml_ctx ctx={
        .ip=method->bytecode,
        .end=method->bytecode+method->bytecode_length,
        .returned=false,
        .return_value=0,
    };
    memset(ctx.locals, 0, sizeof(ctx.locals));
    memset(ctx.args, 0, sizeof(ctx.args));
    for(uint8_t i=0;i<arg_count && i<7;i++){
        ctx.args[i].type=AML_OBJ_INT;
        ctx.args[i].integer=args[i];
    }

    uint32_t steps=0;
    while(ctx.ip<ctx.end && !ctx.returned && steps<4096U){
        struct aml_obj ignored;
        if(!aml_eval_obj(&ctx, &ignored))
            break;
        steps++;
    }
    if(!ctx.returned){
        klog(KLOG_WARN, "acpi-aml: method did not return cleanly");
        return false;
    }
    if(retval)
        *retval=ctx.return_value;
    return true;
}
