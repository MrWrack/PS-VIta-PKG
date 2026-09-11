#include "sfo.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct __attribute__((packed)) {
    uint32_t magic, version, key_off, data_off;
    uint32_t count;
} PsfHeader;
typedef struct __attribute__((packed)) {
    uint16_t key_off, fmt;
    uint32_t len, max_len, data_off;
} PsfEntry;

int sfo_get_string(const char *path, const char *key, char *out, size_t outsz){
    if(!out || outsz==0) return -1; out[0]=0;
    FILE *f=fopen(path,"rb"); if(!f) return -2;
    PsfHeader h; if(fread(&h,1,sizeof(h),f)!=sizeof(h) || h.magic!=0x46535000u){fclose(f);return -3;}
    if(h.count>256){fclose(f);return -4;}
    PsfEntry *e=(PsfEntry*)malloc(sizeof(PsfEntry)*h.count); if(!e){fclose(f);return -5;}
    if(fread(e,sizeof(PsfEntry),h.count,f)!=h.count){free(e);fclose(f);return -6;}
    for(uint32_t i=0;i<h.count;i++){
        char k[128]; fseek(f,(long)h.key_off+e[i].key_off,SEEK_SET);
        size_t j=0; int ch; while(j+1<sizeof(k) && (ch=fgetc(f))!=EOF && ch){k[j++]=(char)ch;} k[j]=0;
        if(strcmp(k,key)==0){
            fseek(f,(long)h.data_off+e[i].data_off,SEEK_SET);
            size_t n=e[i].len; if(n>=outsz)n=outsz-1;
            if(fread(out,1,n,f)!=n){free(e);fclose(f);return -7;}
            out[n]=0; while(n && out[n-1]==0) out[--n]=0;
            free(e);fclose(f);return 0;
        }
    }
    free(e);fclose(f);return -8;
}
