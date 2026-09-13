#include "net_receiver.h"
#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/io/fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define NET_MEMORY_SIZE (1024 * 1024)
#define HEADER_MAGIC 0x56504B31u /* VPK1 */
#define THEME_MAGIC 0x54484D31u /* THM1 */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t name_len;
    uint64_t file_size;
} UploadHeader;

static void *net_memory = NULL;
static int net_ready = 0;

static int recv_all(int s, void *buf, unsigned int len) {
    unsigned char *p = (unsigned char *)buf;
    unsigned int got = 0;
    while (got < len) {
        int r = sceNetRecv(s, p + got, len - got, 0);
        if (r <= 0) return r < 0 ? r : -1;
        got += (unsigned int)r;
    }
    return 0;
}

static void sanitize_filename(char *s) {
    for (char *p = s; *p; ++p) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
            *p = '_';
    }
    if (!strstr(s, ".vpk") && !strstr(s, ".VPK")) {
        size_t n = strlen(s);
        if (n + 4 < 240) strcat(s, ".vpk");
    }
}

int net_receiver_init(char *ip_out, int ip_out_size) {
    if (ip_out && ip_out_size > 0) snprintf(ip_out, ip_out_size, "-");
    if (net_ready) return 0;

    int r = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (r < 0) return r;

    net_memory = malloc(NET_MEMORY_SIZE);
    if (!net_memory) return -1;

    SceNetInitParam p;
    memset(&p, 0, sizeof(p));
    p.memory = net_memory;
    p.size = NET_MEMORY_SIZE;
    p.flags = 0;
    r = sceNetInit(&p);
    if (r < 0) return r;

    r = sceNetCtlInit();
    if (r < 0) return r;

    net_ready = 1;
    if (ip_out && ip_out_size > 0) {
        SceNetCtlInfo info;
        memset(&info, 0, sizeof(info));
        if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) >= 0)
            snprintf(ip_out, ip_out_size, "%s", info.ip_address);
    }
    return 0;
}

void net_receiver_term(void) {
    if (!net_ready) return;
    sceNetCtlTerm();
    sceNetTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    free(net_memory);
    net_memory = NULL;
    net_ready = 0;
}

int net_receive_one_vpk(const char *dest_dir, int port, char *saved_path, int saved_path_size, char *status, int status_size) {
    if (!net_ready) return -1;

    int server = sceNetSocket("VPKManagerRecv", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (server < 0) return server;

    SceNetSockaddrIn addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = SCE_NET_AF_INET;
    addr.sin_port = sceNetHtons((unsigned short)port);
    addr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);

    int r = sceNetBind(server, (SceNetSockaddr *)&addr, sizeof(addr));
    if (r < 0) { sceNetSocketClose(server); return r; }
    r = sceNetListen(server, 1);
    if (r < 0) { sceNetSocketClose(server); return r; }

    if (status) snprintf(status, status_size, "Vantar pa PC pa port %d...", port);
    int client = sceNetAccept(server, NULL, NULL);
    if (client < 0) { sceNetSocketClose(server); return client; }

    UploadHeader h;
    r = recv_all(client, &h, sizeof(h));
    if (r < 0 || h.magic != HEADER_MAGIC || h.name_len == 0 || h.name_len >= 240 || h.file_size == 0) {
        sceNetSocketClose(client); sceNetSocketClose(server); return -2;
    }

    char name[256]; memset(name, 0, sizeof(name));
    r = recv_all(client, name, h.name_len);
    if (r < 0) { sceNetSocketClose(client); sceNetSocketClose(server); return r; }
    name[h.name_len] = '\0'; sanitize_filename(name);

    char path[512]; snprintf(path, sizeof(path), "%s/%s", dest_dir, name);
    char partial_path[520]; snprintf(partial_path, sizeof(partial_path), "%s.part", path);
    sceIoRemove(partial_path);
    SceUID fd = sceIoOpen(partial_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) { sceNetSocketClose(client); sceNetSocketClose(server); return fd; }

    unsigned char buf[32 * 1024];
    uint64_t total = 0;
    while (total < h.file_size) {
        unsigned int want = sizeof(buf);
        if (h.file_size - total < want) want = (unsigned int)(h.file_size - total);
        int n = sceNetRecv(client, buf, want, 0);
        if (n <= 0) { r = -3; break; }
        int w = sceIoWrite(fd, buf, n);
        if (w != n) { r = w < 0 ? w : -4; break; }
        total += (uint64_t)n;
        if (status) snprintf(status, status_size, "Tar emot %s: %llu%%", name, (unsigned long long)((total * 100ULL) / h.file_size));
    }
    sceIoClose(fd);

    uint8_t reply = (r < 0) ? 0 : 1;
    sceNetSend(client, &reply, 1, 0);
    sceNetSocketClose(client);
    sceNetSocketClose(server);

    if (r < 0) {
        /* Keep the .part suffix so an interrupted transfer can never be
           mistaken for an installable VPK. */
        return r;
    }
    sceIoRemove(path);
    r = sceIoRename(partial_path, path);
    if (r < 0) return r;
    if (saved_path) snprintf(saved_path, saved_path_size, "%s", path);
    if (status) snprintf(status, status_size, "Mottagen: %s", name);
    return 0;
}


int net_receive_theme_zip(const char *dest_path, int port, char *status, int status_size) {
    if (!net_ready) return -1;
    int server = sceNetSocket("VPKThemeRecv", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
    if (server < 0) return server;
    SceNetSockaddrIn addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = SCE_NET_AF_INET;
    addr.sin_port = sceNetHtons((unsigned short)port);
    addr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);
    int r = sceNetBind(server, (SceNetSockaddr *)&addr, sizeof(addr));
    if (r < 0) { sceNetSocketClose(server); return r; }
    r = sceNetListen(server, 1);
    if (r < 0) { sceNetSocketClose(server); return r; }
    if (status) snprintf(status, status_size, "Vantar pa tema fran PC pa port %d...", port);
    int client = sceNetAccept(server, NULL, NULL);
    if (client < 0) { sceNetSocketClose(server); return client; }
    UploadHeader h; r = recv_all(client, &h, sizeof(h));
    if (r < 0 || h.magic != THEME_MAGIC || h.name_len == 0 || h.name_len >= 240 || h.file_size == 0) {
        sceNetSocketClose(client); sceNetSocketClose(server); return -2;
    }
    char name[256]; memset(name,0,sizeof(name));
    r = recv_all(client, name, h.name_len);
    if (r < 0) { sceNetSocketClose(client); sceNetSocketClose(server); return r; }
    name[h.name_len] = 0;
    SceUID fd = sceIoOpen(dest_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) { sceNetSocketClose(client); sceNetSocketClose(server); return fd; }
    unsigned char buf[32*1024]; uint64_t total=0; r=0;
    while (total < h.file_size) {
        unsigned int want=sizeof(buf); if (h.file_size-total < want) want=(unsigned int)(h.file_size-total);
        int n=sceNetRecv(client,buf,want,0); if(n<=0){r=-3;break;}
        int w=sceIoWrite(fd,buf,n); if(w!=n){r=w<0?w:-4;break;}
        total += (uint64_t)n;
        if(status) snprintf(status,status_size,"Tar emot tema: %llu%%",(unsigned long long)((total*100ULL)/h.file_size));
    }
    sceIoClose(fd);
    uint8_t reply=(r<0)?0:1; sceNetSend(client,&reply,1,0);
    sceNetSocketClose(client); sceNetSocketClose(server);
    if(r<0){sceIoRemove(dest_path); return r;}
    if(status) snprintf(status,status_size,"Tema mottaget fran PC.");
    return 0;
}
