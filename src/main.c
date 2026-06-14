#if defined(__linux__) && !defined(__ANDROID__)
#define _GNU_SOURCE
#else
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include "fermi/fermi.h"
#include "fearena/arena.h"
#include "felexer/lexer.h"
#include "felexer/token.h"
#include "feparser/parser.h"
#include "fehir/hir.h"
#include "fetc/tc.h"
#include "fesema/sema.h"
#include "fecodegen/codegen.h"
#include "fecodegen/fir.h"
#include "feopt/opt.h"
#include "fellvm/llvm_emit.h"

static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (double)ts.tv_sec*1e3+(double)ts.tv_nsec/1e6;
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *f=fopen(path,"rb");
    if(!f){fprintf(stderr,"[error] cannot open '%s'\n",path);return NULL;}
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *buf=malloc((size_t)sz+2);
    if(!buf){fclose(f);return NULL;}
    size_t r=fread(buf,1,(size_t)sz,f); buf[r]='\0'; fclose(f);
    if(out_len)*out_len=(size_t)r;
    return buf;
}

static const char *find_clang(void) {
    static const char *candidates[]={
        "clang-21","clang-20","clang-19","clang-18","clang-17",
        "clang-16","clang-15","clang-14","clang-13","clang",
        "/data/data/com.termux/files/usr/bin/clang",
        NULL
    };
    char cmd[512];
    for(int i=0;candidates[i];i++){
        if(candidates[i][0]=='/'){
            if(access(candidates[i],X_OK)==0) return candidates[i];
            continue;
        }
        snprintf(cmd,sizeof(cmd),"command -v %s >/dev/null 2>&1",candidates[i]);
        if(system(cmd)==0) return candidates[i];
    }
    return NULL;
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Fermi v%d.%d.%d compiler\n"
        "Usage: %s [options] <file.fe>\n\n"
        "Options:\n"
        "  --llvm          Emit LLVM IR to stdout\n"
        "  --fir           Emit MIR/FIR dump to stdout\n"
        "  --ast           Parse only (no codegen)\n"
        "  --lex           Lex only (dump tokens)\n"
        "  -o <out>        Output executable path\n"
        "  --no-opt        Disable MIR optimizer\n"
        "  --time          Print timing info\n"
        "  --target <t>    LLVM target triple\n"
        "  -O0..3          Optimization level for clang\n"
        "  --version,-v    Print version\n\n",
        V_MAJOR,V_MINOR,V_PATCH,prog);
}

static int make_temp_path(char *buf, size_t buflen, const char *prefix, const char *suffix) {
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || tmpdir[0] == '\0') tmpdir = "/tmp";

    if (suffix && suffix[0] != '\0') {
        char pattern[512];
        snprintf(pattern, sizeof(pattern), "%s/%sXXXXXX%s", tmpdir, prefix ? prefix : "", suffix);
        int suffixlen = (int)strlen(suffix);
#if defined(__GLIBC__) || defined(__ANDROID__) || defined(__APPLE__)
        int fd = mkstemps(pattern, suffixlen);
#else
        int fd = -1;
        {
            char *x = strstr(pattern, "XXXXXX");
            if (x) {
                unsigned seed = (unsigned)time(NULL) ^ (unsigned)getpid();
                for (int attempt = 0; attempt < 100 && fd < 0; attempt++) {
                    unsigned r = seed + (unsigned)attempt * 6364136223846793005u;
                    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
                    for (int j = 0; j < 6; j++) {
                        x[j] = chars[(r >> (j*5)) & 0x1f % (sizeof(chars)-1)];
                    }
                    fd = open(pattern, O_RDWR | O_CREAT | O_EXCL, 0600);
                }
            }
        }
#endif
        if (fd < 0) return -1;
        snprintf(buf, buflen, "%s", pattern);
        return fd;
    } else {
        char pattern[512];
        snprintf(pattern, sizeof(pattern), "%s/%sXXXXXX", tmpdir, prefix ? prefix : "");
        int fd = mkstemp(pattern);
        if (fd < 0) return -1;
        snprintf(buf, buflen, "%s", pattern);
        return fd;
    }
}

int main(int argc, char **argv) {
    if(argc<2){usage(argv[0]);return 1;}
    const char *input=NULL,*output="a.out",*target=NULL,*opt_level="-O2";
    int mode_llvm=0,mode_fir=0,mode_ast=0,mode_lex=0;
    int do_opt=1,do_time=0;
    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--llvm")){mode_llvm=1;}
        else if(!strcmp(argv[i],"--fir")){mode_fir=1;}
        else if(!strcmp(argv[i],"--ast")){mode_ast=1;}
        else if(!strcmp(argv[i],"--lex")){mode_lex=1;}
        else if(!strcmp(argv[i],"--opt")){do_opt=1;}
        else if(!strcmp(argv[i],"--no-opt")){do_opt=0;}
        else if(!strcmp(argv[i],"--time")){do_time=1;}
        else if(!strcmp(argv[i],"--version")||!strcmp(argv[i],"-v")){
            printf("fermi %d.%d.%d\n",V_MAJOR,V_MINOR,V_PATCH);return 0;
        }
        else if(!strcmp(argv[i],"-o")&&i+1<argc){output=argv[++i];}
        else if(!strcmp(argv[i],"--target")&&i+1<argc){target=argv[++i];}
        else if(!strncmp(argv[i],"-O",2)){opt_level=argv[i];}
        else if(argv[i][0]=='-'){fprintf(stderr,"warning: unknown option: %s\n",argv[i]);}
        else if(!input){input=argv[i];}
    }
    if(!input){fprintf(stderr,"error: no input file\n");usage(argv[0]);return 1;}

    size_t src_len=0;
    char *src=read_file(input,&src_len);
    if(!src){ fprintf(stderr,"error: cannot open '%s'\n",input); return 1; }

    double t0=now_ms();

    Arena arena_val=arena_new(64*1024*1024);
    Arena *arena=&arena_val;

    if(mode_lex){
        Lexer l2=lexer_new(src,src_len,arena);
        Token tok;
        do{ tok=lexer_next(&l2); token_print(&tok); }while(tok.type!=TOK_EOF);
        free(src); arena_free(arena); return 0;
    }

    Parser p=parser_new(src,src_len,arena);
    AstNode *prog=parse_program(&p);

    if(p.had_error){
        free(src);arena_free(arena);return 1;
    }
    if(mode_ast){
        printf("[ast] program parsed ok\n");
        free(src);arena_free(arena);return 0;
    }

    Hir hir=hir_new(arena);
    hir_lower(&hir,prog);

    Tc tc=tc_new(arena);
    tc_check(&tc,prog);
    if(tc.had_error){free(src);arena_free(arena);return 1;}

    Sema sema=sema_new(arena);
    sema_check(&sema,prog);
    if(sema.had_error){free(src);arena_free(arena);return 1;}

    double tcg=now_ms();
    Codegen *cg=codegen_new(arena);
    codegen_emit(cg,prog);
    FirModule *mod=codegen_module(cg);
    double tcg_end=now_ms();

    if(do_opt){
        mir_opt(mod,arena);
    }

    if(mode_fir){
        fir_print_module(mod);
        free(src);arena_free(arena);return 0;
    }

    FILE *out_f=NULL;
    char tmp_ll[512];
    tmp_ll[0]='\0';

    if(mode_llvm){
        out_f=stdout;
    }else{
        int fd = make_temp_path(tmp_ll, sizeof(tmp_ll), "fermi_", ".ll");
        if(fd < 0){
            fprintf(stderr,"error: cannot create temp .ll file (TMPDIR=%s)\n",
                    getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
            free(src);arena_free(arena);return 1;
        }
        out_f = fdopen(fd, "w");
        if(!out_f){
            close(fd); unlink(tmp_ll);
            fprintf(stderr,"error: fdopen failed for '%s'\n", tmp_ll);
            free(src);arena_free(arena);return 1;
        }
    }

    double tllvm=now_ms();
    llvm_emit_module(mod,arena,out_f,target);
    double tllvm_end=now_ms();

    if(!mode_llvm) fclose(out_f);

    if(do_time){
        fprintf(stderr,"[time] codegen:   %.3fms\n",tcg_end-tcg);
        fprintf(stderr,"[time] llvm-emit: %.3fms\n",tllvm_end-tllvm);
        fprintf(stderr,"[time] total:     %.3fms\n",tllvm_end-t0);
    }

    if(!mode_llvm){
        const char *clang=find_clang();
        if(!clang){
            unlink(tmp_ll);
            free(src);arena_free(arena);
            fprintf(stderr,"error: no clang found; install clang to compile Fermi programs\n");
            return 1;
        }

        char tmp_obj[512];
        int ofd = make_temp_path(tmp_obj, sizeof(tmp_obj), "fermi_", ".o");
        if(ofd < 0){
            unlink(tmp_ll);free(src);arena_free(arena);
            fprintf(stderr,"error: cannot create temp .o file\n");
            return 1;
        }
        close(ofd);

        char cmd[4096];

        snprintf(cmd,sizeof(cmd),"%s %s -c -x ir '%s' -o '%s' 2>&1",
            clang,opt_level,tmp_ll,tmp_obj);
        int rc=system(cmd);
        unlink(tmp_ll);
        if(rc!=0){
            unlink(tmp_obj);free(src);arena_free(arena);
            fprintf(stderr,"error: compilation failed (clang exit %d)\n",rc);
            return 1;
        }

        snprintf(cmd,sizeof(cmd),"cc -o '%s' '%s' -lm 2>&1",output,tmp_obj);
        rc=system(cmd);
        unlink(tmp_obj);
        free(src);arena_free(arena);
        if(rc!=0){fprintf(stderr,"error: link failed\n");return 1;}
        return 0;
    }

    free(src);arena_free(arena);
    return 0;
}
