#include "ziputil.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>

#define SIG_EOCD 0x06054b50u
#define SIG_CEN  0x02014b50u
#define SIG_LOC  0x04034b50u

static uint16_t rd16(const unsigned char *p){ return (uint16_t)(p[0] | (p[1]<<8)); }
static uint32_t rd32(const unsigned char *p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }

static int mkdirs(const char *path) {
    char tmp[768];
    size_t n = strlen(path);
    if (n >= sizeof(tmp)) return -1;
    strcpy(tmp, path);
    for (size_t i=0; i<n; ++i) {
        if (tmp[i]=='/' && i>4) {
            char c=tmp[i]; tmp[i]=0;
            sceIoMkdir(tmp, 0777);
            tmp[i]=c;
        }
    }
    return 0;
}

static int safe_rel(const char *name) {
    if (!name || !*name) return 0;
    if (name[0]=='/' || strchr(name, ':')) return 0;
    if (strstr(name, "../") || strstr(name, "..\\")) return 0;
    return 1;
}

static int find_eocd(FILE *f, uint32_t *cd_off, uint16_t *count) {
    if (fseek(f, 0, SEEK_END)!=0) return -1;
    long end=ftell(f); if (end < 22) return -1;
    long span=end > (0xFFFF+22) ? (0xFFFF+22) : end;
    unsigned char *buf=(unsigned char*)malloc((size_t)span); if(!buf) return -1;
    fseek(f,end-span,SEEK_SET);
    if (fread(buf,1,(size_t)span,f)!=(size_t)span){ free(buf); return -1; }
    for(long i=span-22;i>=0;--i){
        if(rd32(buf+i)==SIG_EOCD){
            *count=rd16(buf+i+10);
            *cd_off=rd32(buf+i+16);
            free(buf); return 0;
        }
    }
    free(buf); return -1;
}

int zip_foreach(const char *zip_path, zip_iter_cb cb, void *user) {
    FILE *f=fopen(zip_path,"rb"); if(!f) return -1;
    uint32_t cd=0; uint16_t count=0;
    if(find_eocd(f,&cd,&count)<0){ fclose(f); return -2; }
    if(fseek(f,(long)cd,SEEK_SET)!=0){ fclose(f); return -3; }
    for(unsigned i=0;i<count;i++){
        unsigned char h[46];
        if(fread(h,1,46,f)!=46 || rd32(h)!=SIG_CEN){ fclose(f); return -4; }
        uint16_t fn=rd16(h+28), ex=rd16(h+30), cm=rd16(h+32);
        ZipEntry e; memset(&e,0,sizeof(e));
        e.flags=rd16(h+8); e.method=rd16(h+10); e.crc32=rd32(h+16);
        e.comp_size=rd32(h+20); e.uncomp_size=rd32(h+24); e.local_offset=rd32(h+42);
        size_t keep=fn < sizeof(e.name)-1 ? fn : sizeof(e.name)-1;
        if(fread(e.name,1,keep,f)!=keep){ fclose(f); return -5; }
        e.name[keep]=0;
        if(fn>keep) fseek(f,(long)(fn-keep),SEEK_CUR);
        fseek(f,(long)ex+cm,SEEK_CUR);
        long next=ftell(f);
        int r=cb(&e,user);
        if(r!=0){ fclose(f); return r; }
        fseek(f,next,SEEK_SET);
    }
    fclose(f); return 0;
}

static int extract_entry(FILE *zf, const ZipEntry *e, const char *out_path) {
    if(e->flags & 1) return -20; /* encrypted */
    if(e->method!=0 && e->method!=8) return -21;
    if(fseek(zf,(long)e->local_offset,SEEK_SET)!=0) return -22;
    unsigned char lh[30];
    if(fread(lh,1,30,zf)!=30 || rd32(lh)!=SIG_LOC) return -23;
    uint16_t fn=rd16(lh+26), ex=rd16(lh+28);
    if(fseek(zf,(long)fn+ex,SEEK_CUR)!=0) return -24;
    mkdirs(out_path);
    FILE *out=fopen(out_path,"wb"); if(!out) return -25;
    unsigned char in[32768], outb[32768];
    unsigned long left=e->comp_size;
    int result=0;
    if(e->method==0){
        while(left){ size_t n=left>sizeof(in)?sizeof(in):(size_t)left; if(fread(in,1,n,zf)!=n){result=-26;break;} if(fwrite(in,1,n,out)!=n){result=-27;break;} left-=n; }
    } else {
        z_stream s; memset(&s,0,sizeof(s));
        if(inflateInit2(&s,-MAX_WBITS)!=Z_OK){ fclose(out); return -28; }
        int zr=Z_OK;
        while(left && zr!=Z_STREAM_END){
            size_t n=left>sizeof(in)?sizeof(in):(size_t)left;
            if(fread(in,1,n,zf)!=n){result=-29;break;}
            left-=n; s.next_in=in; s.avail_in=(uInt)n;
            while(s.avail_in && zr!=Z_STREAM_END){
                s.next_out=outb; s.avail_out=sizeof(outb);
                zr=inflate(&s,Z_NO_FLUSH);
                if(zr!=Z_OK && zr!=Z_STREAM_END){result=-30;break;}
                size_t have=sizeof(outb)-s.avail_out;
                if(have && fwrite(outb,1,have,out)!=have){result=-31;break;}
            }
            if(result) break;
        }
        inflateEnd(&s);
        if(!result && zr!=Z_STREAM_END) result=-32;
    }
    fclose(out);
    if(result) sceIoRemove(out_path);
    return result;
}

typedef struct { const char *want; ZipEntry found; int hit; } FindCtx;
static int find_cb(const ZipEntry *e, void *u){ FindCtx *c=(FindCtx*)u; if(strcmp(e->name,c->want)==0){c->found=*e;c->hit=1;return 1;} return 0; }

int zip_extract_named(const char *zip_path, const char *entry_name, const char *out_path) {
    FindCtx c={entry_name,{0},0};
    int r=zip_foreach(zip_path,find_cb,&c);
    if(!c.hit) return -10;
    (void)r;
    FILE *f=fopen(zip_path,"rb"); if(!f) return -11;
    r=extract_entry(f,&c.found,out_path); fclose(f); return r;
}

typedef struct { FILE *f; const char *dest; int err; } AllCtx;
static int all_cb(const ZipEntry *e, void *u){
    AllCtx *c=(AllCtx*)u;
    if(!safe_rel(e->name)){ c->err=-40; return c->err; }
    char out[1024]; snprintf(out,sizeof(out),"%s/%s",c->dest,e->name);
    size_t n=strlen(e->name);
    if(n && e->name[n-1]=='/') { mkdirs(out); sceIoMkdir(out,0777); return 0; }
    c->err=extract_entry(c->f,e,out);
    return c->err;
}

int zip_extract_all(const char *zip_path, const char *dest_dir) {
    sceIoMkdir(dest_dir,0777);
    FILE *f=fopen(zip_path,"rb"); if(!f) return -1;
    AllCtx c={f,dest_dir,0};
    int r=zip_foreach(zip_path,all_cb,&c);
    fclose(f);
    return c.err ? c.err : r;
}
