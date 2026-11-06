// Original CPU cache-contract fixture. The generated include is the unmodified
// production gfx_shader.c TU, with its real headers and SHA-256 implementation.
// Platform files/locks are real except an opt-in, one-write migration failure.
// Translator children and background workers are forbidden; preparation runs
// synchronously when its thread provider returns NULL.
#define plat_child_start fixture_child_start
#define plat_child_stop fixture_child_stop
#define plat_child_write fixture_child_write
#define plat_child_read fixture_child_read
#define plat_thread_new fixture_thread_new
#define plat_thread_spawn fixture_thread_spawn
#define plat_file_write_atomic fixture_atomic_write
#include "@SHADER_SOURCE@"
#include "nvn_geometry_control.h"
#undef plat_file_write_atomic
// Reuse the real header's function type; the normal symbol is linked from the
// platform TU. Parser, codec, SHA and cache functions are never replaced.
extern __typeof__(fixture_atomic_write) plat_file_write_atomic;
#include "sha256.h"
#include <stdarg.h>

static void forbidden(const char *name) { fprintf(stderr, "forbidden fixture provider: %s\n", name); abort(); }
static char failed_migration_path[PATH_CAP+40];
static int migration_attempts, injected_failures;
int fixture_atomic_write(const char *path,const void *data,size_t n) {
    if(failed_migration_path[0] && !strcmp(path,failed_migration_path)) {
        migration_attempts++;
        if(!injected_failures) {
            RecHeader header;
            if(n<sizeof header)forbidden("short migration write");
            memcpy(&header,data,sizeof header);
            if(memcmp(header.magic,"TOTKSANS",8)||header.version!=2||header.rec_size!=217)
                forbidden("failure target is not the v1-to-v2 migration");
            injected_failures++;return 0;
        }
    }
    return plat_file_write_atomic(path,data,n);
}
PlatChild *fixture_child_start(const char *a, const char *b, uint32_t *e) {
    (void)a; (void)b; (void)e; forbidden("translator process"); return NULL;
}
void fixture_child_stop(PlatChild *p) { (void)p; forbidden("translator stop"); }
int fixture_child_write(PlatChild *p, const void *d, size_t n) {
    (void)p; (void)d; (void)n; forbidden("translator write"); return 0;
}
long fixture_child_read(PlatChild *p, void *d, size_t n) {
    (void)p; (void)d; (void)n; forbidden("translator read"); return 0;
}
PlatThread *fixture_thread_new(PlatThreadFn fn, void *arg, const char *name) {
    (void)fn; (void)arg; (void)name; return NULL;
}
int fixture_thread_spawn(PlatThreadFn fn, void *arg, const char *name, size_t size) {
    (void)fn; (void)arg; (void)name; (void)size; forbidden("background thread"); return 0;
}
const char *cfg_get(const char *name) { (void)name; return NULL; }
void rt_log(const char *format, ...) { (void)format; }
_Noreturn void rt_fatal(const char *format, ...) { (void)format; forbidden("runtime fatal"); abort(); }
void rt_sha256(const void *data, size_t size, uint8_t out[32]) { sha256(data, size, out); }
int nvn2_lower(const uint8_t *control, const uint8_t *code, const uint8_t **out_control, const uint8_t **out_code) {
    (void)control; (void)code; (void)out_control; (void)out_code;
    forbidden("NVN 2 lowering"); return 0;
}
int kern_core_away(void) { forbidden("guest-core release"); return 0; }
void kern_core_back(int *away) { (void)away; forbidden("guest-core reacquire"); }
uint64_t perf_begin(void) { forbidden("translation timing"); return 0; }
void perf_end(int kind, uint64_t start) { (void)kind; (void)start; forbidden("translation timing"); }

static void setup(const char *directory) {
    if (strlen(directory) >= sizeof cache_dir) abort();
    strcpy(cache_dir, directory);
    // Every path is the new owned fixture cache supplied by Python. Suppress
    // executable/config discovery and the optional background .use preloader.
    paths_once.state = 2;
    for (unsigned i = 0; i < sizeof xlate_id; i++) xlate_id[i] = (uint8_t)(0x80 + i);
    for (unsigned i = 0; i < sizeof host_sg / sizeof *host_sg; i++) host_sg[i] = 32;
    plat_file_mkdir_p(cache_dir);
#if FIXTURE_GEOMETRY
    const char *subgroup=getenv("FIXTURE_GS_SUBGROUP");
    uint32_t lanes=subgroup?(uint32_t)strtoul(subgroup,NULL,10):32;
    if(lanes)gfx_shader_set_geometry_host(lanes);
#endif
}

static void answers(GfxShaderAnswers *a, unsigned geometry) {
    memset(a, 0, sizeof *a);
    for (unsigned i = 0; i < 32; i++) {
        a->tex_type[i] = (uint8_t)(i % 10 + 1);
        a->img_type[i] = (uint8_t)(i % 10 + 1);
        a->img_format[i] = (uint8_t)(i % 50 + 1);
        a->attr_type[i] = (uint8_t)(i % 3);
    }
    for (unsigned i = 0; i < 8; i++) a->frag_out_type[i] = (uint8_t)(i % 3);
    a->local[0] = 4; a->local[1] = 2; a->local[2] = 1;
    a->shared_bytes = 0x11223344; a->depth_minus_w = 1;
#if FIXTURE_GEOMETRY
    a->geometry_input = (uint8_t)geometry;
#else
    (void)geometry;
#endif
}

static int code_hash(int stage, const uint8_t *control, const uint8_t *code, uint64_t size,
                     const uint8_t *c1, uint32_t c1_size, unsigned input, uint8_t hash[32]) {
    (void)input;
    return @HASH_CALL@;
}

static void hex_bytes(const uint8_t *bytes, size_t size) {
    for (size_t i = 0; i < size; i++) printf("%02x", bytes[i]);
}

static void summary(const GfxShader *s, int owned) {
    printf("{\"accepted\":%s,\"owned\":%d",s?"true":"false",owned);
    if(s) {
        printf(",\"stage\":%d,\"inputs\":%u,\"outputs\":%u",gfx_shader_stage(s),gfx_shader_inputs(s,NULL),gfx_shader_outputs(s,NULL));
        printf(",\"iid\":\"");hex_bytes(gfx_shader_input_id(s),32);printf("\"");
        const Nvn2C0Layout *c0=gfx_shader_c0_layout(s);
        printf(",\"has_c0\":%s",c0?"true":"false");
        if(c0)printf(",\"c0_ssbos\":%u,\"c0_root0\":%u",c0->ssbos,c0->roots[0]);
#if FIXTURE_GEOMETRY
        const GfxShaderGeometry *g=gfx_shader_geometry(s);
        if(g)printf(",\"input\":%u,\"invocations\":%u,\"max_vertices\":%u",g->input,g->invocations,g->max_output_vertices);
#endif
    }
    puts("}");
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    setup(argv[2]);
    if (!strcmp(argv[1], "reflect-flags")) {
        if(argc!=5)return 2;
        int accepted[2]={0,0};
        for(unsigned i=0;i<2;i++) {
            uint64_t sn=0,rn=0;GfxShader *s=calloc(1,sizeof *s);
            s->spirv=(uint32_t *)read_file(argv[3],&sn);s->reflection=(char *)read_file(argv[4],&rn);
            if(!s->spirv||!s->reflection||sn%4)return 3;
            s->words=sn/4;s->reflection_size=rn;
            accepted[i]=reflect(s,i?REFLECT_LOWERED_BODY:0);shader_free(s);
        }
        printf("{\"strict\":%s,\"lowered\":%s}\n",accepted[0]?"true":"false",accepted[1]?"true":"false");return 0;
    }
    if (!strcmp(argv[1], "pack-pass") || !strcmp(argv[1], "get-pass")) {
        if(argc!=7)return 2;
        uint64_t cn=0,nn=0;uint8_t *code=read_file(argv[3],&cn),*control=read_file(argv[4],&nn);
        if(!code||!control||nn!=CONTROL_SIZE)return 3;
        uint32_t words[8],got[8];const char *p=argv[5];
        for(unsigned i=0;i<8;i++) {char *end;unsigned long long n=strtoull(p,&end,0);
            if(end==p||n>UINT32_MAX||*end!=(i==7?'\0':','))return 3;words[i]=(uint32_t)n;p=end+1;}
        int packed=!strcmp(argv[1],"get-pass")?0:nvn_geometry_control_pack_portable(control,(size_t)nn,code,cn,words);
        int read=nvn_geometry_control_get(control,(size_t)nn,code,cn,got);
        if(!write_file(argv[6],control,(size_t)nn))return 4;
        printf("{\"accepted\":%s,\"status\":%d,\"skip\":[",packed?"true":"false",read);
        for(unsigned i=0;i<8;i++)printf("%s%u",i?",":"",got[i]);puts("]}");free(code);free(control);return 0;
    }
    if (!strcmp(argv[1], "seed-pass") || !strcmp(argv[1], "record-pass") || !strcmp(argv[1], "seed-lowered")) {
        if(argc!=7)return 2;
        uint64_t cn=0,nn=0,sn=0,rn=0;uint8_t *code=read_file(argv[3],&cn),*control=read_file(argv[4],&nn);
        GfxShader *s=calloc(1,sizeof *s);s->spirv=(uint32_t *)read_file(argv[5],&sn);s->reflection=(char *)read_file(argv[6],&rn);
        s->words=sn/4;s->reflection_size=rn;
        int lowered=!strcmp(argv[1],"seed-lowered");
        uint32_t skip[8];
        if(lowered && (!code||!control||nn!=CONTROL_SIZE||
           nvn_geometry_control_get(control,nn,code,cn,skip)!=1||
           !nvn_geometry_control_pack_lowered(control,nn,code,cn,skip)))return 3;
        GfxShaderAnswers a;answers(&a,4);Req req={0};
        if(!code||!control||nn!=CONTROL_SIZE||!s->spirv||sn%4||!s->reflection||
           !reflect(s,lowered?REFLECT_LOWERED_BODY:0)||!req_init(&req,3,control,code,cn,NULL,&a)) {puts("{\"accepted\":false}");shader_free(s);free(code);free(control);return 0;}
        UseMask use=use_of(s);use_store(req.code_hash,&use);
        if(!req_init(&req,3,control,code,cn,NULL,&a))return 4;
        memcpy(s->key,req.key,32);memcpy(s->iid,req.iid,32);
        char hex[65],path[PATH_CAP+80];key_hex(req.key,hex);
        snprintf(path,sizeof path,"%s/%s.spv",cache_dir,hex);if(!write_file(path,s->spirv,(size_t)sn))return 4;
        snprintf(path,sizeof path,"%s/%s.res.json",cache_dir,hex);if(!write_file(path,s->reflection,(size_t)rn))return 4;
        in_store(req.iid,3,control,code,&req,&a);
        if(lowered && !write_file(argv[4],control,(size_t)nn))return 4;
        if(!strcmp(argv[1],"record-pass")) {
            char keys[PATH_CAP+40];snprintf(keys,sizeof keys,"%s/pipeline-keys.bin",cache_dir);
            gfx_shader_record_at(keys);ans_note(&req,3,control,code,&a,s);gfx_shader_record_flush(1);
        }
        printf("{\"accepted\":true,\"hash\":\"");hex_bytes(req.code_hash,32);printf("\",\"iid\":\"");hex_bytes(req.iid,32);
        printf("\",\"key\":\"");hex_bytes(req.key,32);puts("\"}");shader_free(s);free(code);free(control);return 0;
    }
    if (!strcmp(argv[1], "identity")) {
        if (argc != 7) return 2;
        int stage = atoi(argv[3]); unsigned input = (unsigned)atoi(argv[4]);
        uint64_t code_size = 0, control_size = 0;
        uint8_t *code = read_file(argv[5], &code_size), *control = read_file(argv[6], &control_size);
        if (!code || !control || control_size != CONTROL_SIZE) return 3;
        GfxShaderAnswers a; answers(&a, input);
        UseMask mask = {.slots=9, .attrs=33, .fouts=3};
        uint8_t hash[32], key[32], iid[32], provisional[32], name[32], packed[256];
        const uint8_t constants[8] = {0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0};
        if (!code_hash(stage, control, code, code_size, constants, sizeof constants, input, hash)) return 4;
        build_key(hash, &a, &mask, key, iid);
        build_key(hash, &a, NULL, provisional, NULL);
        use_name(hash, name);
        UseMask learned; int known=use_find(hash,&learned);
        printf("{\"hash\":\""); hex_bytes(hash,32); printf("\",\"key\":\""); hex_bytes(key,32);
        printf("\",\"iid\":\""); hex_bytes(iid,32); printf("\",\"provisional\":\""); hex_bytes(provisional,32);
        printf("\",\"use\":\""); hex_bytes(name,32); printf("\",\"answers\":\"");
        hex_bytes(packed, pack_answers(packed,&a)); printf("\",\"known\":%d,\"key_version\":%u}\n",known,KEY_VERSION);
        free(code); free(control); return 0;
    }
    if (!strcmp(argv[1], "learn")) {
        if(argc!=4)return 2;uint8_t hash[32];if(!hex32(argv[3],hash))return 3;
        UseMask m={.slots=9,.attrs=33,.fouts=3};use_store(hash,&m);puts("{}");return 0;
    }
    if (!strcmp(argv[1], "input-key")) {
        if(argc!=4)return 2;uint8_t iid[32],key[32];if(!hex32(argv[3],iid))return 3;
        key_of_input(iid,key);printf("{\"key\":\"");hex_bytes(key,32);puts("\"}");return 0;
    }
    if (!strcmp(argv[1], "load")) {
        if(argc!=6)return 2;uint8_t key[32],iid[32];if(!hex32(argv[4],key)||!hex32(argv[5],iid))return 3;
        int packed=strstr(argv[3],"packed")!=NULL,owned=0;
        if(packed)pack_open();
        if(strstr(argv[3],"memory")) {
            GfxShader *cached=disk_load(key,iid,0);if(cached)mem_insert(cached);
            if(strstr(argv[3],"missing")) {char path[PATH_CAP+80];in_path(iid,path);plat_file_delete(path);}
        }
        const GfxShader *s;
        if(!strncmp(argv[3],"private",7))s=gfx_shader_load_private(iid,&owned);
        else if(!strncmp(argv[3],"input",5))s=gfx_shader_load_input(iid,strstr(argv[3],"translate")!=NULL);
        else {s=disk_load_from(key,iid,packed,0);owned=s!=NULL;}
        summary(s,owned);if(owned)shader_free((GfxShader *)s);return 0;
    }
    if (!strcmp(argv[1], "load-input") || !strcmp(argv[1],"load-input-packed")) {
        if(argc!=4)return 2;uint8_t iid[32];if(!hex32(argv[3],iid))return 3;
        if(strstr(argv[1],"packed"))pack_open();
        Inputs in={0};int ok=in_load(iid,&in);
        printf("{\"accepted\":%s",ok?"true":"false");
        if(ok) {
            printf(",\"stage\":%d,\"input\":%u,\"answers\":\"",in.stage,
#if FIXTURE_GEOMETRY
                   in.a.geometry_input
#else
                   0u
#endif
            );uint8_t packed[256];hex_bytes(packed,pack_answers(packed,&in.a));printf("\"");
            Req r={0};int recreated=req_init(&r,in.stage,in.control,in.code,in.length,in.c1,&in.a);
            printf(",\"recreated\":%s",recreated?"true":"false");
            if(recreated) {
                char request[32768];build_request(request,"original",r.stage_name,"original Maxwell.code",r.c1,r.c1_len,&in.a,in.control);
                printf(",\"request\":%s,\"iid\":\"",request);hex_bytes(r.iid,32);printf("\"");
            }
        }puts("}");free(in.buf);return 0;
    }
    if (!strcmp(argv[1],"prepare")) {
        if(argc!=3)return 2;int status=gfx_shader_prepare_seed(cache_dir,1,1);
        char keys[PATH_CAP+40];snprintf(keys,sizeof keys,"%s/pipeline-keys.bin",cache_dir);gfx_shader_record_at(keys);
        GfxWarmEnt *entries=NULL;size_t count=gfx_shader_warm_list(&entries);
        printf("{\"accepted\":%s,\"failures\":%lld,\"entries\":[",status?"false":"true",(long long)n_failures);
        for(size_t i=0;i<count;i++) {
            if(i)printf(",");printf("{\"iid\":\"");hex_bytes(entries[i].iid,32);
            printf("\",\"stage\":%u,\"priority\":%u}",entries[i].stage,entries[i].prio);
        }puts("]}");free(entries);return 0;
    }
    if (!strcmp(argv[1], "answers")) {
        if(argc!=5)return 2;uint8_t *body=NULL;size_t n=0;int ok;
#if FIXTURE_GEOMETRY
        ok=answers_read(argv[3],&body,&n,atoi(argv[4]));
#else
        ok=rec_read(argv[3],"TOTKSANS",1,216,&body,&n);
#endif
        printf("{\"accepted\":%s,\"body\":\"",ok?"true":"false");hex_bytes(body,n);puts("\"}");free(body);return 0;
    }
    if (!strcmp(argv[1], "request") || !strcmp(argv[1],"request-load") || !strcmp(argv[1],"request-memory") ||
        !strcmp(argv[1],"store-input") || !strcmp(argv[1],"record-append") || !strcmp(argv[1],"record-atomic-failure")) {
        if (argc != 7) return 2;
        int stage=atoi(argv[3]); unsigned input=(unsigned)atoi(argv[4]);
        uint64_t code_size=0,control_size=0;
        uint8_t *code=read_file(argv[5],&code_size),*control=read_file(argv[6],&control_size);
        if(!code||!control||control_size!=CONTROL_SIZE)return 3;
        GfxShaderAnswers a; answers(&a,input); Req req={0};
        if(!req_init(&req,stage,control,code,code_size,NULL,&a)) {puts("{\"accepted\":false}");return 0;}
        if(!strcmp(argv[1],"request-load") || !strcmp(argv[1],"request-memory")) {
            pack_open();
            if(!strcmp(argv[1],"request-memory")) {
                GfxShader *cached=disk_load(req.key,req.iid,req.require_c0);
                if(cached)mem_insert(cached);
                if(!cached){summary(NULL,0);return 0;}
                const GfxShader *s=get_one(&req,stage,control,code,&a);
                summary(s,0);free(code);free(control);return 0;
            }
#if FIXTURE_GEOMETRY
            GfxShader *s=request_disk_load(&req,req.key,req.iid);
#else
            GfxShader *s=disk_load(req.key,req.iid,req.require_c0);
#endif
            summary(s,s!=NULL);if(s)shader_free(s);free(code);free(control);return 0;
        }
        if(!strcmp(argv[1],"store-input") || !strcmp(argv[1],"record-append") || !strcmp(argv[1],"record-atomic-failure")) {
            UseMask mask={.slots=9,.attrs=33,.fouts=3};use_store(req.code_hash,&mask);
            if(!req_init(&req,stage,control,code,code_size,NULL,&a))return 5;
            in_store(req.iid,stage,control,code,&req,&a);
            if(!strcmp(argv[1],"record-append") || !strcmp(argv[1],"record-atomic-failure")) {
                char keys[PATH_CAP+40];snprintf(keys,sizeof keys,"%s/pipeline-keys.bin",cache_dir);
                if(!strcmp(argv[1],"record-atomic-failure"))
                    snprintf(failed_migration_path,sizeof failed_migration_path,"%s/shader-answers.bin",cache_dir);
                gfx_shader_record_at(keys);GfxShader shader={0};memcpy(shader.iid,req.iid,32);
                ans_note(&req,stage,control,code,&a,&shader);gfx_shader_record_flush(1);
                if(failed_migration_path[0]) {
                    printf("{\"accepted\":true,\"recording\":%d,\"queued_answers\":%zu,\"queued_programs\":%zu,"
                           "\"migration_attempts\":%d,\"injected_failures\":%d,\"record_version\":%d}\n",
                           (int)RC.on,RC.ans.n,RC.prg.n,migration_attempts,injected_failures,ANS_VERSION);
                    free(code);free(control);return 0;
                }
            }
        }
        char request[32768];
        build_request(request,"original",req.stage_name,"original Maxwell.code",req.c1,req.c1_len,&a,control);
        printf("{\"accepted\":true,\"request\":%s,\"hash\":\"",request);hex_bytes(req.code_hash,32);
        printf("\",\"key\":\"");hex_bytes(req.key,32);printf("\",\"iid\":\"");hex_bytes(req.iid,32);
        printf("\",\"final\":%d}\n",req.final);
        free(code);free(control);return 0;
    }
    return 2;
}
