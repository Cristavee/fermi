#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "llvm_emit.h"

typedef struct StrConst StrConst;
struct StrConst { char *content; char *gname; int idx; StrConst *next; };

typedef struct { StrConst *strs; int str_count; Arena *arena; FILE *out; } LlvmCtx;

static StrConst *intern_str(LlvmCtx *ctx, const char *content) {
    for (StrConst *s=ctx->strs;s;s=s->next)
        if(strcmp(s->content,content)==0) return s;
    StrConst *sc=arena_alloc(ctx->arena,sizeof(StrConst));
    sc->content=arena_strdup(ctx->arena,content);
    sc->idx=ctx->str_count++;
    char buf[32]; snprintf(buf,sizeof(buf),".str.%d",sc->idx);
    sc->gname=arena_strdup(ctx->arena,buf);
    sc->next=ctx->strs; ctx->strs=sc;
    return sc;
}
static void collect_strs(LlvmCtx *ctx, FirModule *m) {
    for(FirFn *fn=m->fns;fn;fn=fn->next)
        for(FirBlock *b=fn->blocks;b;b=b->next)
            for(FirInstr *i=b->head;i;i=i->next)
                if(i->op==FIR_CONST_STR&&i->src1) intern_str(ctx,i->src1);
}
static int str_escaped_len(const char *s) {
    int n=0; for(;*s;s++) n++; return n+1;
}
static void emit_str_char(FILE *f, unsigned char c) {
    if(isprint(c)&&c!='"'&&c!='\\') fputc(c,f);
    else fprintf(f,"\\%02X",(unsigned)c);
}
static void emit_str_globals(LlvmCtx *ctx) {
    for(StrConst *sc=ctx->strs;sc;sc=sc->next) {
        int len=str_escaped_len(sc->content);
        fprintf(ctx->out,"@%s = private unnamed_addr constant [%d x i8] c\"",sc->gname,len);
        for(const char *p=sc->content;*p;p++) emit_str_char(ctx->out,(unsigned char)*p);
        fprintf(ctx->out,"\\00\"\n");
    }
    if(ctx->strs) fprintf(ctx->out,"\n");
}
static void emit_struct_decls(LlvmCtx *ctx, FirModule *m) {
    for(FirStructDecl *sd=m->struct_decls;sd;sd=sd->next) {
        if(sd->nfields==0) continue;
        fprintf(ctx->out,"%%struct.%s = type { ",sd->name);
        for(int i=0;i<sd->nfields;i++) {
            if(i) fprintf(ctx->out,", ");
            fprintf(ctx->out,"%s",sd->field_types[i]);
        }
        fprintf(ctx->out," }\n");
    }
    if(m->struct_decls) fprintf(ctx->out,"\n");
}

static const char *libc_decls[] = {
    "declare i32 @printf(ptr noundef, ...)",
    "declare i32 @fprintf(ptr noundef, ptr noundef, ...)",
    "declare i32 @sprintf(ptr noundef, ptr noundef, ...)",
    "declare i32 @snprintf(ptr noundef, i64, ptr noundef, ...)",
    "declare i32 @scanf(ptr noundef, ...)",
    "declare i32 @sscanf(ptr noundef, ptr noundef, ...)",
    "declare i32 @puts(ptr noundef)",
    "declare i32 @putchar(i32)",
    "declare i32 @getchar()",
    "declare ptr @fgets(ptr noundef, i32, ptr noundef)",
    "declare i32 @fputc(i32, ptr noundef)",
    "declare i32 @fputs(ptr noundef, ptr noundef)",
    "declare ptr @fopen(ptr noundef, ptr noundef)",
    "declare i32 @fclose(ptr noundef)",
    "declare i64 @fread(ptr noundef, i64, i64, ptr noundef)",
    "declare i64 @fwrite(ptr noundef, i64, i64, ptr noundef)",
    "declare ptr @malloc(i64)",
    "declare ptr @calloc(i64, i64)",
    "declare ptr @realloc(ptr, i64)",
    "declare void @free(ptr)",
    "declare ptr @memcpy(ptr noundef, ptr noundef, i64)",
    "declare ptr @memmove(ptr noundef, ptr noundef, i64)",
    "declare ptr @memset(ptr noundef, i32, i64)",
    "declare i32 @memcmp(ptr noundef, ptr noundef, i64)",
    "declare i64 @strlen(ptr noundef)",
    "declare i32 @strcmp(ptr noundef, ptr noundef)",
    "declare i32 @strncmp(ptr noundef, ptr noundef, i64)",
    "declare ptr @strcpy(ptr noundef, ptr noundef)",
    "declare ptr @strncpy(ptr noundef, ptr noundef, i64)",
    "declare ptr @strcat(ptr noundef, ptr noundef)",
    "declare ptr @strncat(ptr noundef, ptr noundef, i64)",
    "declare ptr @strstr(ptr noundef, ptr noundef)",
    "declare ptr @strchr(ptr noundef, i32)",
    "declare ptr @strrchr(ptr noundef, i32)",
    "declare ptr @strtok(ptr, ptr noundef)",
    "declare ptr @strerror(i32)",
    "declare i32 @atoi(ptr noundef)",
    "declare i64 @atol(ptr noundef)",
    "declare double @atof(ptr noundef)",
    "declare i64 @strtol(ptr noundef, ptr, i32)",
    "declare double @strtod(ptr noundef, ptr)",
    "declare double @sqrt(double)",
    "declare double @cbrt(double)",
    "declare double @pow(double, double)",
    "declare double @exp(double)",
    "declare double @exp2(double)",
    "declare double @log(double)",
    "declare double @log2(double)",
    "declare double @log10(double)",
    "declare double @sin(double)",
    "declare double @cos(double)",
    "declare double @tan(double)",
    "declare double @asin(double)",
    "declare double @acos(double)",
    "declare double @atan(double)",
    "declare double @atan2(double, double)",
    "declare double @floor(double)",
    "declare double @ceil(double)",
    "declare double @round(double)",
    "declare double @trunc(double)",
    "declare double @fabs(double)",
    "declare double @fmod(double, double)",
    "declare double @fmin(double, double)",
    "declare double @fmax(double, double)",
    "declare double @hypot(double, double)",
    "declare float @sqrtf(float)",
    "declare float @powf(float, float)",
    "declare float @fabsf(float)",
    "declare float @floorf(float)",
    "declare float @ceilf(float)",
    "declare float @roundf(float)",
    "declare float @sinf(float)",
    "declare float @cosf(float)",
    "declare float @tanf(float)",
    "declare i32 @abs(i32)",
    "declare i64 @labs(i64)",
    "declare void @exit(i32) noreturn",
    "declare i32 @system(ptr noundef)",
    "declare ptr @getenv(ptr noundef)",
    "declare i32 @setenv(ptr noundef, ptr noundef, i32)",
    "declare i32 @rand()",
    "declare void @srand(i32)",
    "declare i64 @read(i32, ptr, i64)",
    "declare i32 @clock_gettime(i32, ptr)",
    "declare i32 @nanosleep(ptr, ptr)",
    "declare ptr @getcwd(ptr, i64)",
    "declare i32 @chdir(ptr noundef)",
    "@stderr = external global ptr",
    "@stdin = external global ptr",
    "@stdout = external global ptr",
};
#define NLIBC_DECLS (int)(sizeof(libc_decls)/sizeof(libc_decls[0]))

static int is_user_fn(FirModule *m, const char *name) {
    for(FirFn *fn=m->fns;fn;fn=fn->next)
        if(strcmp(fn->name,name)==0) return 1;
    return 0;
}
static int module_calls(FirModule *m, const char *name) {
    for(FirFn *fn=m->fns;fn;fn=fn->next)
        for(FirBlock *b=fn->blocks;b;b=b->next)
            for(FirInstr *i=b->head;i;i=i->next)
                if((i->op==FIR_CALL||i->op==FIR_CALL_INDIRECT)&&i->src1&&strcmp(i->src1,name)==0) return 1;
    return 0;
}

static int is_float_type(const char *ty) {
    return ty&&(strcmp(ty,"float")==0||strcmp(ty,"double")==0);
}
static int is_numeric_lit(const char *s) {
    if(!s||!*s) return 0;
    const char *p=s; if(*p=='-')p++;
    if(!isdigit((unsigned char)*p)) return 0;
    while(isdigit((unsigned char)*p))p++;
    if(*p=='.'){p++;while(isdigit((unsigned char)*p))p++;}
    if(*p=='e'||*p=='E'){p++;if(*p=='+'||*p=='-')p++;while(isdigit((unsigned char)*p))p++;}
    return *p=='\0';
}
static void emit_float_hex(FILE *f, double v, int is_double) {
    union{double d;uint64_t u;} uu;
    if(is_double){uu.d=v;}else{float fv=(float)v;uu.d=(double)fv;}
    fprintf(f,"0x%016llX",(unsigned long long)uu.u);
}
static void emit_operand(LlvmCtx *ctx, const char *s, const char *ty) {
    if(!s||!*s){fprintf(ctx->out,"0");return;}
    if(s[0]=='%'){fprintf(ctx->out,"%s",s);return;}
    if(s[0]=='t'&&isdigit((unsigned char)s[1])){fprintf(ctx->out,"%%%s",s);return;}
    if(s[0]=='@'){fprintf(ctx->out,"%s",s);return;}
    if(strncmp(s,"%%arg.",6)==0){fprintf(ctx->out,"%%%s",s+2);return;}
    if(is_float_type(ty)&&is_numeric_lit(s)){
        emit_float_hex(ctx->out,atof(s),strcmp(ty,"double")==0);
        return;
    }
    if(strcmp(s,"null")==0){fprintf(ctx->out,"null");return;}
    fprintf(ctx->out,"%s",s);
}
static void emit_ptr_op(LlvmCtx *ctx, const char *s) {
    if(!s||!*s){fprintf(ctx->out,"null");return;}
    if(s[0]=='%'){fprintf(ctx->out,"%s",s);return;}
    if(s[0]=='@'){fprintf(ctx->out,"%s",s);return;}
    if(strcmp(s,"null")==0){fprintf(ctx->out,"null");return;}
    fprintf(ctx->out,"%%%s",s);
}

static void emit_input_helper(FILE *f) {
    fprintf(f,"@__fermi_ibuf = internal global [4097 x i8] zeroinitializer\n\n");
    fprintf(f,"define internal ptr @__fermi_input() {\n");
    fprintf(f,"entry:\n");
    fprintf(f,"  %%n = call i64 @read(i32 0, ptr @__fermi_ibuf, i64 4096)\n");
    fprintf(f,"  %%np = getelementptr [4097 x i8], ptr @__fermi_ibuf, i32 0, i64 %%n\n");
    fprintf(f,"  store i8 0, ptr %%np, align 1\n");
    fprintf(f,"  %%has_n = icmp sgt i64 %%n, 0\n");
    fprintf(f,"  br i1 %%has_n, label %%chk_nl, label %%done\n");
    fprintf(f,"chk_nl:\n");
    fprintf(f,"  %%nl_idx = sub i64 %%n, 1\n");
    fprintf(f,"  %%nl_ptr = getelementptr [4097 x i8], ptr @__fermi_ibuf, i32 0, i64 %%nl_idx\n");
    fprintf(f,"  %%nl_ch = load i8, ptr %%nl_ptr, align 1\n");
    fprintf(f,"  %%is_nl = icmp eq i8 %%nl_ch, 10\n");
    fprintf(f,"  br i1 %%is_nl, label %%strip, label %%done\n");
    fprintf(f,"strip:\n");
    fprintf(f,"  store i8 0, ptr %%nl_ptr, align 1\n");
    fprintf(f,"  br label %%done\n");
    fprintf(f,"done:\n");
    fprintf(f,"  ret ptr @__fermi_ibuf\n");
    fprintf(f,"}\n\n");
}

static void emit_concat_helper(FILE *f) {
    fprintf(f,"define internal ptr @__fermi_concat(ptr %%a, ptr %%b) {\n");
    fprintf(f,"entry:\n");
    fprintf(f,"  %%la = call i64 @strlen(ptr %%a)\n");
    fprintf(f,"  %%lb = call i64 @strlen(ptr %%b)\n");
    fprintf(f,"  %%tot = add i64 %%la, %%lb\n");
    fprintf(f,"  %%sz = add i64 %%tot, 1\n");
    fprintf(f,"  %%r = call ptr @malloc(i64 %%sz)\n");
    fprintf(f,"  call ptr @strcpy(ptr %%r, ptr %%a)\n");
    fprintf(f,"  call ptr @strcat(ptr %%r, ptr %%b)\n");
    fprintf(f,"  ret ptr %%r\n");
    fprintf(f,"}\n\n");
}

static void emit_extern_fns(LlvmCtx *ctx, FirModule *m) {
    for(FirFn *fn=m->fns;fn;fn=fn->next){
        if(!fn->is_extern) continue;
        fprintf(ctx->out,"declare %s @%s(",fn->ret_type?fn->ret_type:"void",fn->name);
        int first=1;
        for(FirParam *p=fn->params;p;p=p->next){
            if(!first)fprintf(ctx->out,", "); first=0;
            fprintf(ctx->out,"%s",p->type);
        }
        fprintf(ctx->out,")\n");
    }
}

static void emit_fn(LlvmCtx *ctx, FirFn *fn) {
    if(fn->is_extern) return;
    FILE *f=ctx->out;
    fprintf(f,"define %s @%s(",fn->ret_type?fn->ret_type:"void",fn->name);
    int first=1;
    for(FirParam *p=fn->params;p;p=p->next) {
        if(!first){ fprintf(f,", "); } first=0;
        fprintf(f,"%s %%arg.%s",p->type,p->name);
    }
    /* nounwind: no C++ exceptions — lets LLVM inline across call sites */
    fprintf(f,") nounwind {\n");
    for(FirBlock *b=fn->blocks;b;b=b->next) {
        if(b->dead) continue;
        if(b==fn->blocks) fprintf(f,"entry:\n");
        else fprintf(f,"%s:\n",b->label);
        if(!b->head){fprintf(f,"  unreachable\n");continue;}
        int has_term=0;
        for(FirInstr *i=b->head;i;i=i->next){
            if(i->op==FIR_BR||i->op==FIR_BR_COND||i->op==FIR_RET||
               i->op==FIR_RET_VOID||i->op==FIR_UNREACHABLE)
                has_term=1;
            fprintf(f,"  ");
            switch(i->op){
            case FIR_ALLOCA:
                fprintf(f,"%%%s = alloca %s, align 8\n",i->dst,i->type); break;
            case FIR_STORE:
                fprintf(f,"store %s ",i->type);
                emit_operand(ctx,i->src1,i->type);
                fprintf(f,", ptr ");
                emit_ptr_op(ctx,i->dst);
                fprintf(f,", align 8\n"); break;
            case FIR_LOAD:
                fprintf(f,"%%%s = load %s, ptr %%%s, align 8\n",i->dst,i->type,i->src1); break;
            case FIR_ADD:
                fprintf(f,"%%%s = add %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_SUB:
                fprintf(f,"%%%s = sub %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_MUL:
                fprintf(f,"%%%s = mul %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_DIV:
                fprintf(f,"%%%s = sdiv %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_UDIV:
                fprintf(f,"%%%s = udiv %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_MOD:
                fprintf(f,"%%%s = srem %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_UREM:
                fprintf(f,"%%%s = urem %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_FADD:
                fprintf(f,"%%%s = fadd %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_FSUB:
                fprintf(f,"%%%s = fsub %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_FMUL:
                fprintf(f,"%%%s = fmul %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_FDIV:
                fprintf(f,"%%%s = fdiv %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_ICMP:
                fprintf(f,"%%%s = icmp %s %s ",i->dst,i->src3?i->src3:"eq",i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_FCMP:
                fprintf(f,"%%%s = fcmp %s %s ",i->dst,i->src3?i->src3:"oeq",i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_AND:
                fprintf(f,"%%%s = and %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_OR:
                fprintf(f,"%%%s = or %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_XOR:
                fprintf(f,"%%%s = xor %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_SHL:
                fprintf(f,"%%%s = shl %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_SHR:
                fprintf(f,"%%%s = ashr %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_LSHR:
                fprintf(f,"%%%s = lshr %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,", ");
                emit_operand(ctx,i->src2,i->type);fprintf(f,"\n"); break;
            case FIR_BR:
                fprintf(f,"br label %%%s\n",i->src1); break;
            case FIR_BR_COND:
                fprintf(f,"br i1 ");
                emit_operand(ctx,i->src1,"i1");
                fprintf(f,", label %%%s, label %%%s\n",i->src2,i->src3); break;
            case FIR_CALL: {
                if(i->dst&&i->dst[0]&&strcmp(i->type?i->type:"void","void")!=0)
                    fprintf(f,"%%%s = call %s @%s(",i->dst,i->type,i->src1);
                else
                    fprintf(f,"call %s @%s(",i->type?i->type:"void",i->src1);
                for(int j=0;j<i->nargs;j++){
                    if(j)fprintf(f,", ");
                    fprintf(f,"%s ",i->arg_types[j]);
                    if(strcmp(i->arg_types[j],"ptr")==0)
                        emit_ptr_op(ctx,i->args[j]);
                    else
                        emit_operand(ctx,i->args[j],i->arg_types[j]);
                }
                fprintf(f,")\n"); break;
            }
            case FIR_RET:
                fprintf(f,"ret %s ",i->type);
                emit_operand(ctx,i->src1,i->type);
                fprintf(f,"\n"); break;
            case FIR_RET_VOID:
                fprintf(f,"ret void\n"); break;
            case FIR_UNREACHABLE:
                fprintf(f,"unreachable\n"); break;
            case FIR_ZEXT:
                fprintf(f,"%%%s = zext %s ",i->dst,i->src2);
                emit_operand(ctx,i->src1,i->src2);
                fprintf(f," to %s\n",i->type); break;
            case FIR_SEXT:
                fprintf(f,"%%%s = sext %s ",i->dst,i->src2);
                emit_operand(ctx,i->src1,i->src2);
                fprintf(f," to %s\n",i->type); break;
            case FIR_TRUNC:
                fprintf(f,"%%%s = trunc %s ",i->dst,i->src2);
                emit_operand(ctx,i->src1,i->src2);
                fprintf(f," to %s\n",i->type); break;
            case FIR_FPEXT:
                fprintf(f,"%%%s = fpext %s ",i->dst,i->src2);
                emit_operand(ctx,i->src1,i->src2);
                fprintf(f," to %s\n",i->type); break;
            case FIR_FPTRUNC:
                fprintf(f,"%%%s = fptrunc %s ",i->dst,i->src2);
                emit_operand(ctx,i->src1,i->src2);
                fprintf(f," to %s\n",i->type); break;
            case FIR_SITOFP:
                fprintf(f,"%%%s = sitofp %s ",i->dst,i->src2);
                emit_operand(ctx,i->src1,i->src2);
                fprintf(f," to %s\n",i->type); break;
            case FIR_UITOFP:
                fprintf(f,"%%%s = uitofp %s ",i->dst,i->src2);
                emit_operand(ctx,i->src1,i->src2);
                fprintf(f," to %s\n",i->type); break;
            case FIR_FPTOSI:
                fprintf(f,"%%%s = fptosi %s ",i->dst,i->src2);
                emit_operand(ctx,i->src1,i->src2);
                fprintf(f," to %s\n",i->type); break;
            case FIR_FPTOUI:
                fprintf(f,"%%%s = fptoui %s ",i->dst,i->src2);
                emit_operand(ctx,i->src1,i->src2);
                fprintf(f," to %s\n",i->type); break;
            case FIR_PTRTOINT:
                fprintf(f,"%%%s = ptrtoint ptr ",i->dst);
                emit_ptr_op(ctx,i->src1);
                fprintf(f," to %s\n",i->type); break;
            case FIR_INTTOPTR:
                fprintf(f,"%%%s = inttoptr %s ",i->dst,i->src2);
                emit_operand(ctx,i->src1,i->src2);
                fprintf(f," to ptr\n"); break;
            case FIR_BITCAST:
                fprintf(f,"%%%s = bitcast %s ",i->dst,i->src2);
                emit_operand(ctx,i->src1,i->src2);
                fprintf(f," to %s\n",i->type); break;
            case FIR_NEG:
                fprintf(f,"%%%s = sub %s 0, ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,"\n"); break;
            case FIR_FNEG:
                fprintf(f,"%%%s = fneg %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);fprintf(f,"\n"); break;
            case FIR_SELECT:
                fprintf(f,"%%%s = select i1 ",i->dst);
                emit_operand(ctx,i->src1,"i1");
                fprintf(f,", %s ",i->type);
                emit_operand(ctx,i->src2,i->type);
                fprintf(f,", %s ",i->type);
                emit_operand(ctx,i->src3,i->type);
                fprintf(f,"\n"); break;
            case FIR_CONST_STR: {
                StrConst *sc=intern_str(ctx,i->src1);
                int len=str_escaped_len(sc->content);
                fprintf(f,"%%%s = getelementptr [%d x i8], ptr @%s, i32 0, i32 0\n",
                        i->dst,len,sc->gname);
                break;
            }
            case FIR_PTR_ADD:
                fprintf(f,"%%%s = getelementptr i8, ptr ",i->dst);
                emit_ptr_op(ctx,i->src1);
                fprintf(f,", i64 ");
                emit_operand(ctx,i->src2,"i64");
                fprintf(f,"\n"); break;
            case FIR_GEP:
                fprintf(f,"%%%s = getelementptr %s, ptr ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);
                fprintf(f,", i32 ");
                emit_operand(ctx,i->src2,"i32");
                fprintf(f,"\n"); break;
            case FIR_GEP_STRUCT:
                fprintf(f,"%%%s = getelementptr inbounds %%struct.%s, ptr ",i->dst,i->type);
                emit_ptr_op(ctx,i->src1);
                fprintf(f,", i32 0, i32 %s\n",i->src3?i->src3:"0"); break;
            case FIR_BITNOT:
                fprintf(f,"%%%s = xor %s ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);
                fprintf(f,", -1\n"); break;
            case FIR_PHI: {
                fprintf(f,"%%%s = phi %s [ ",i->dst,i->type);
                emit_operand(ctx,i->src1,i->type);
                fprintf(f,", %%%s ]",i->src2 ? i->src2 : "entry");
                if(i->nargs > 0 && i->args[0]) {
                    fprintf(f,", [ ");
                    emit_operand(ctx,i->src3,i->type);
                    fprintf(f,", %%%s ]",i->args[0]);
                }
                fprintf(f,"\n"); break;
            }
            case FIR_CALL_INDIRECT: {
                if(i->dst&&i->dst[0]&&strcmp(i->type?i->type:"void","void")!=0)
                    fprintf(f,"%%%s = call %s (",i->dst,i->type);
                else
                    fprintf(f,"call %s (",i->type?i->type:"void");
                for(int j=0;j<i->nargs;j++){
                    if(j)fprintf(f,", ");
                    fprintf(f,"%s",i->arg_types[j]);
                }
                fprintf(f,") ");
                emit_ptr_op(ctx,i->src1);
                fprintf(f,"(");
                for(int j=0;j<i->nargs;j++){
                    if(j)fprintf(f,", ");
                    fprintf(f,"%s ",i->arg_types[j]);
                    emit_operand(ctx,i->args[j],i->arg_types[j]);
                }
                fprintf(f,")\n"); break;
            }
            case FIR_MEMCPY: {
                fprintf(f,"call ptr @memcpy(ptr ");
                emit_ptr_op(ctx,i->dst);
                fprintf(f,", ptr ");
                emit_ptr_op(ctx,i->src1);
                fprintf(f,", i64 ");
                emit_operand(ctx,i->src2,"i64");
                fprintf(f,")\n"); break;
            }
            case FIR_MEMSET_I: {
                fprintf(f,"call ptr @memset(ptr ");
                emit_ptr_op(ctx,i->dst);
                fprintf(f,", i32 ");
                emit_operand(ctx,i->src1,"i32");
                fprintf(f,", i64 ");
                emit_operand(ctx,i->src2,"i64");
                fprintf(f,")\n"); break;
            }
            case FIR_NOP: break;
            default:
                fprintf(f,"; unhandled op %d\n",i->op); break;
            }
        }
        if(!has_term) fprintf(f,"  unreachable\n");
    }
    fprintf(f,"}\n\n");
}

void llvm_emit_module(FirModule *m, Arena *arena, FILE *out, const char *target) {
    LlvmCtx ctx={NULL,0,arena,out};
    collect_strs(&ctx,m);

    if(target&&*target)
        fprintf(out,"target triple = \"%s\"\n",target);
    else
        fprintf(out,"target triple = \"x86_64-unknown-linux-gnu\"\n");
    fprintf(out,"target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128\"\n\n");

    emit_struct_decls(&ctx,m);
    emit_str_globals(&ctx);

    for(int d=0;d<NLIBC_DECLS;d++){
        const char *decl=libc_decls[d];
        if(decl[0]=='@') { fprintf(out,"%s\n",decl); continue; }
        char fn_part[128]; int dp=0;
        const char *at=strchr(decl,'@');
        if(at){
            at++;
            while(*at&&*at!='('&&*at!=' '&&dp<127) fn_part[dp++]=*at++;
            fn_part[dp]='\0';
            if(is_user_fn(m,fn_part)) continue;
        }
        fprintf(out,"%s\n",decl);
    }
    fprintf(out,"\n");

    emit_extern_fns(&ctx,m);

    if(module_calls(m,"__fermi_input")) emit_input_helper(out);
    if(module_calls(m,"__fermi_concat")) emit_concat_helper(out);

    {
        static const struct { const char *decl; const char *name; } rt_decls[] = {
            {"declare void @fe_print_bool(i32)","fe_print_bool"},
            {"declare void @fe_print_i32(i32)","fe_print_i32"},
            {"declare void @fe_print_i64(i64)","fe_print_i64"},
            {"declare void @fe_print_f32(float)","fe_print_f32"},
            {"declare void @fe_print_f64(double)","fe_print_f64"},
            {"declare void @fe_print_str(ptr)","fe_print_str"},
            {"declare void @fe_println_bool(i32)","fe_println_bool"},
            {"declare void @fe_println_i32(i32)","fe_println_i32"},
            {"declare void @fe_println_i64(i64)","fe_println_i64"},
            {"declare void @fe_println_f32(float)","fe_println_f32"},
            {"declare void @fe_println_f64(double)","fe_println_f64"},
            {"declare void @fe_println_str(ptr)","fe_println_str"},
            {"declare void @fe_flush()","fe_flush"},
            {"declare ptr @fe_input()","fe_input"},
            {"declare i64 @fe_time()","fe_time"},
            {"declare void @fe_sleep(i64)","fe_sleep"},
            {"declare i32 @fe_len(ptr)","fe_len"},
            {"declare ptr @fe_concat(ptr, ptr)","fe_concat"},
            {"declare ptr @fe_to_upper(ptr)","fe_to_upper"},
            {"declare ptr @fe_to_lower(ptr)","fe_to_lower"},
            {"declare ptr @fe_trim(ptr)","fe_trim"},
            {"declare i32 @fe_contains(ptr, ptr)","fe_contains"},
            {"declare i32 @fe_starts_with(ptr, ptr)","fe_starts_with"},
            {"declare i32 @fe_ends_with(ptr, ptr)","fe_ends_with"},
            {"declare ptr @fe_replace(ptr, ptr, ptr)","fe_replace"},
            {"declare i32 @fe_index_of(ptr, ptr)","fe_index_of"},
            {"declare ptr @fe_slice(ptr, i32, i32)","fe_slice"},
            {"declare i32 @fe_parse_int(ptr)","fe_parse_int"},
            {"declare float @fe_parse_float(ptr)","fe_parse_float"},
            {"declare ptr @fe_int_to_str(i32)","fe_int_to_str"},
            {"declare ptr @fe_float_to_str(float)","fe_float_to_str"},
            {"declare i32 @fe_abs(i32)","fe_abs"},
            {"declare float @fe_absf(float)","fe_absf"},
            {"declare float @fe_sqrt(float)","fe_sqrt"},
            {"declare float @fe_powf(float, float)","fe_powf"},
            {"declare float @fe_floor(float)","fe_floor"},
            {"declare float @fe_ceil(float)","fe_ceil"},
            {"declare float @fe_round(float)","fe_round"},
            {"declare i32 @fe_min(i32, i32)","fe_min"},
            {"declare i32 @fe_max(i32, i32)","fe_max"},
            {"declare float @fe_minf(float, float)","fe_minf"},
            {"declare float @fe_maxf(float, float)","fe_maxf"},
            {"declare i32 @fe_clamp(i32, i32, i32)","fe_clamp"},
            {"declare float @fe_sin(float)","fe_sin"},
            {"declare float @fe_cos(float)","fe_cos"},
            {"declare float @fe_tan(float)","fe_tan"},
            {"declare float @fe_log(float)","fe_log"},
            {"declare float @fe_log2f(float)","fe_log2f"},
            {"declare float @fe_log10f(float)","fe_log10f"},
            {"declare ptr @fe_alloc(i64)","fe_alloc"},
            {"declare ptr @fe_realloc(ptr, i64)","fe_realloc"},
            {"declare void @fe_memcopy(ptr, ptr, i64)","fe_memcopy"},
            {"declare void @fe_memset(ptr, i32, i64)","fe_memset"},
            {"declare i32 @fe_memcmp(ptr, ptr, i64)","fe_memcmp"},
            {"declare void @fe_exit(i32) noreturn","fe_exit"},
            {"declare ptr @fe_getenv(ptr)","fe_getenv"},
            {"declare i32 @fe_setenv(ptr, ptr)","fe_setenv"},
            {"declare ptr @fe_getcwd()","fe_getcwd"},
            {"declare i32 @fe_chdir(ptr)","fe_chdir"},
            {"declare ptr @fe_read_file(ptr)","fe_read_file"},
            {"declare void @fe_write_file(ptr, ptr)","fe_write_file"},
            {"declare void @fe_region_push()","fe_region_push"},
            {"declare ptr @fe_region_alloc(i64)","fe_region_alloc"},
            {"declare void @fe_region_pop()","fe_region_pop"},
            {"declare ptr @fe_array_new(i64)","fe_array_new"},
            {"declare void @fe_array_push(ptr, i64)","fe_array_push"},
            {"declare i64 @fe_array_pop(ptr)","fe_array_pop"},
            {"declare i64 @fe_array_get(ptr, i64)","fe_array_get"},
            {"declare void @fe_array_set(ptr, i64, i64)","fe_array_set"},
            {"declare i64 @fe_array_len(ptr)","fe_array_len"},
            {"declare void @fe_array_free(ptr)","fe_array_free"},
            {"declare ptr @fe_map_new()","fe_map_new"},
            {"declare void @fe_map_set(ptr, ptr, i64)","fe_map_set"},
            {"declare i64 @fe_map_get(ptr, ptr)","fe_map_get"},
            {"declare i32 @fe_map_has(ptr, ptr)","fe_map_has"},
            {"declare void @fe_map_delete(ptr, ptr)","fe_map_delete"},
            {"declare void @fe_map_free(ptr)","fe_map_free"},
            {NULL,NULL}
        };
        int emitted_any = 0;
        for(int k=0; rt_decls[k].name; k++) {
            if(module_calls(m, rt_decls[k].name) && !is_user_fn(m, rt_decls[k].name)) {
                fprintf(out,"%s\n",rt_decls[k].decl);
                emitted_any = 1;
            }
        }
        if(emitted_any) fprintf(out,"\n");
    }

    for(FirFn *fn=m->fns;fn;fn=fn->next) emit_fn(&ctx,fn);
}
