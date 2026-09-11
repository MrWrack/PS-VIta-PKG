#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/promoterutil.h>

#include <vita2d.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "ziputil.h"
#include "sfo.h"
#include "net_receiver.h"

#define DOWNLOAD_DIR "ux0:/download"
#define DATA_DIR "ux0:/data/vpk_manager"
#define PREVIEW_DIR DATA_DIR "/preview"
#define INSTALL_DIR DATA_DIR "/install"
#define THEME_DIR DATA_DIR "/theme"
#define THEME_CFG THEME_DIR "/theme.ini"
#define THEME_BG THEME_DIR "/background.png"
#define MAX_FILES 256
#define NAME_LEN 256
#define PC_INSTALL_PORT 1338
#define PC_THEME_PORT 1339
#define THEME_ZIP DATA_DIR "/theme_upload.zip"

typedef struct {
    char name[NAME_LEN];
    char path[512];
    unsigned long long size;
} VpkEntry;

typedef struct {
    char title[128];
    char titleid[32];
    char version[32];
    int valid;
} VpkMeta;

static VpkEntry files[MAX_FILES];
static int file_count = 0;
static int selected = 0;
static int scroll = 0;
static VpkMeta meta;
static vita2d_texture *preview_icon = NULL;
static char status_line[256] = "Klar.";
static char vita_ip[32] = "-";

typedef struct {
    unsigned int bg;
    unsigned int panel;
    unsigned int accent;
    unsigned int text;
    unsigned int selected;
    char name[32];
} AppTheme;

static AppTheme theme;
static vita2d_texture *theme_bg = NULL;
static int settings_open = 0;
static int theme_choice = 0;


static unsigned int hex_rgb(const char *s, unsigned int fallback) {
    unsigned int v = 0;
    if (!s) return fallback;
    if (*s == '#') s++;
    if (sscanf(s, "%06x", &v) != 1) return fallback;
    return RGBA8((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff, 255);
}
static void set_builtin_theme(int choice) {
    theme_choice = choice;
    if (choice == 1) {
        theme.bg = RGBA8(8,16,11,255); theme.panel = RGBA8(18,38,25,255);
        theme.accent = RGBA8(80,255,140,255); theme.text = RGBA8(245,245,245,255);
        theme.selected = RGBA8(40,80,55,255); snprintf(theme.name,sizeof(theme.name),"Green");
    } else if (choice == 2) {
        theme.bg = RGBA8(16,10,24,255); theme.panel = RGBA8(34,22,48,255);
        theme.accent = RGBA8(190,110,255,255); theme.text = RGBA8(245,245,245,255);
        theme.selected = RGBA8(76,44,105,255); snprintf(theme.name,sizeof(theme.name),"Purple");
    } else {
        theme.bg = RGBA8(12,12,14,255); theme.panel = RGBA8(23,23,27,255);
        theme.accent = RGBA8(80,200,255,255); theme.text = RGBA8(245,245,245,255);
        theme.selected = RGBA8(35,65,85,255); snprintf(theme.name,sizeof(theme.name),"Blue");
    }
}

static void free_theme_bg(void) {
    if (theme_bg) { vita2d_free_texture(theme_bg); theme_bg = NULL; }
}

static int load_custom_theme(void) {
    set_builtin_theme(0);
    snprintf(theme.name,sizeof(theme.name),"Custom");
    FILE *f = fopen(THEME_CFG, "r");
    if (f) {
        char line[160];
        while (fgets(line, sizeof(line), f)) {
            char key[64]={0}, val[80]={0};
            if (sscanf(line, " %63[^=]= %79[^\r\n]", key, val) == 2) {
                if (!strcmp(key,"bg")) theme.bg = hex_rgb(val, theme.bg);
                else if (!strcmp(key,"panel")) theme.panel = hex_rgb(val, theme.panel);
                else if (!strcmp(key,"accent")) theme.accent = hex_rgb(val, theme.accent);
                else if (!strcmp(key,"text")) theme.text = hex_rgb(val, theme.text);
                else if (!strcmp(key,"selected") || !strcmp(key,"highlight")) theme.selected = hex_rgb(val, theme.selected);
                else if (!strcmp(key,"name")) snprintf(theme.name,sizeof(theme.name),"%.31s",val);
            }
        }
        fclose(f);
    }
    free_theme_bg();
    theme_bg = vita2d_load_PNG_file(THEME_BG);
    theme_choice = 3;
    snprintf(status_line,sizeof(status_line),"Custom theme laddat.");
    return 0;
}

static void apply_theme_choice(int choice) {
    free_theme_bg();
    if (choice == 3) load_custom_theme(); else set_builtin_theme(choice);
    vita2d_set_clear_color(theme.bg);
}

static int ends_with_vpk(const char *name) {
    size_t n = strlen(name);
    if (n < 4) return 0;
    const char *e = name + n - 4;
    return (e[0] == '.') && (e[1] == 'v' || e[1] == 'V') &&
           (e[2] == 'p' || e[2] == 'P') && (e[3] == 'k' || e[3] == 'K');
}

static void rm_tree(const char *path) {
    SceUID d = sceIoDopen(path);
    if (d < 0) { sceIoRemove(path); return; }
    SceIoDirent ent; memset(&ent, 0, sizeof(ent));
    while (sceIoDread(d, &ent) > 0) {
        if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, "..")) { memset(&ent,0,sizeof(ent)); continue; }
        char p[1024]; snprintf(p, sizeof(p), "%s/%s", path, ent.d_name);
        if (SCE_S_ISDIR(ent.d_stat.st_mode)) rm_tree(p); else sceIoRemove(p);
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);
    sceIoRmdir(path);
}

static void ensure_dirs(void) {
    sceIoMkdir("ux0:/data", 0777);
    sceIoMkdir(DATA_DIR, 0777);
    sceIoMkdir(THEME_DIR, 0777);
}

static void scan_vpks(void) {
    file_count = 0;
    SceUID d = sceIoDopen(DOWNLOAD_DIR);
    if (d < 0) {
        snprintf(status_line, sizeof(status_line), "Kan inte oppna %s (0x%08X)", DOWNLOAD_DIR, d);
        return;
    }
    SceIoDirent ent; memset(&ent, 0, sizeof(ent));
    while (file_count < MAX_FILES && sceIoDread(d, &ent) > 0) {
        if (strcmp(ent.d_name, ".") != 0 &&
            strcmp(ent.d_name, "..") != 0 &&
            ends_with_vpk(ent.d_name)) {

            snprintf(files[file_count].name,
                     sizeof(files[file_count].name),
                     "%s", ent.d_name);

            snprintf(files[file_count].path,
                     sizeof(files[file_count].path),
                     "%s/%s", DOWNLOAD_DIR, ent.d_name);

            SceIoStat st;
            memset(&st, 0, sizeof(st));
            if (sceIoGetstat(files[file_count].path, &st) >= 0)
                files[file_count].size = st.st_size;
            else
                files[file_count].size = ent.d_stat.st_size;

            file_count++;
        }

        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);
    if (selected >= file_count) selected = file_count > 0 ? file_count - 1 : 0;
    if (scroll > selected) scroll = selected;
}

static void clear_preview(void) {
    if (preview_icon) { vita2d_free_texture(preview_icon); preview_icon = NULL; }
    memset(&meta, 0, sizeof(meta));
    rm_tree(PREVIEW_DIR);
}

static void load_preview(void) {
    clear_preview();
    if (file_count <= 0) return;
    sceIoMkdir(PREVIEW_DIR, 0777);
    char icon_path[512], sfo_path[512];
    snprintf(icon_path, sizeof(icon_path), "%s/icon0.png", PREVIEW_DIR);
    snprintf(sfo_path, sizeof(sfo_path), "%s/param.sfo", PREVIEW_DIR);

    int ir = zip_extract_named(files[selected].path, "sce_sys/icon0.png", icon_path);
    int sr = zip_extract_named(files[selected].path, "sce_sys/param.sfo", sfo_path);
    if (sr == 0) {
        if (sfo_get_string(sfo_path, "TITLE", meta.title, sizeof(meta.title)) < 0)
            snprintf(meta.title, sizeof(meta.title), "%s", files[selected].name);
        sfo_get_string(sfo_path, "TITLE_ID", meta.titleid, sizeof(meta.titleid));
        sfo_get_string(sfo_path, "APP_VER", meta.version, sizeof(meta.version));
        meta.valid = 1;
    }
    if (ir == 0) preview_icon = vita2d_load_PNG_file(icon_path);
}

static void refresh_vpk_list(void) {
    int old_selected = selected;

    scan_vpks();

    if (file_count <= 0) {
        selected = 0;
        scroll = 0;
        clear_preview();
        snprintf(status_line, sizeof(status_line),
                 "Inga VPK-filer hittades i ux0:/download/");
        return;
    }

    if (old_selected >= 0 && old_selected < file_count)
        selected = old_selected;
    else
        selected = file_count - 1;

    if (selected < scroll)
        scroll = selected;
    if (selected >= scroll + 8)
        scroll = selected - 7;

    load_preview();

    snprintf(status_line, sizeof(status_line),
             "%d VPK-fil%s hittad%s i ux0:/download/",
             file_count,
             file_count == 1 ? "" : "er",
             file_count == 1 ? "" : "e");
}

static int delete_selected(void) {
    if (file_count <= 0) return -1;
    char old[NAME_LEN]; snprintf(old, sizeof(old), "%s", files[selected].name);
    int r = sceIoRemove(files[selected].path);
    if (r >= 0) snprintf(status_line, sizeof(status_line), "Tog bort %s", old);
    else snprintf(status_line, sizeof(status_line), "Kunde inte ta bort filen: 0x%08X", r);
    scan_vpks(); load_preview();
    return r;
}

static int install_selected(void) {
    if (file_count <= 0) return -1;
    snprintf(status_line, sizeof(status_line), "Packar upp %s...", files[selected].name);
    rm_tree(INSTALL_DIR);
    sceIoMkdir(INSTALL_DIR, 0777);
    int r = zip_extract_all(files[selected].path, INSTALL_DIR);
    if (r < 0) {
        snprintf(status_line, sizeof(status_line), "Fel vid uppackning: %d", r);
        rm_tree(INSTALL_DIR);
        return r;
    }
    snprintf(status_line, sizeof(status_line), "Installerar %s...", meta.title[0] ? meta.title : files[selected].name);
    r = scePromoterUtilityPromotePkg(INSTALL_DIR, 1);
    if (r >= 0) snprintf(status_line, sizeof(status_line), "Installation klar.");
    else snprintf(status_line, sizeof(status_line), "Installationen misslyckades: 0x%08X", r);
    rm_tree(INSTALL_DIR);
    return r;
}


static int select_path(const char *path) {
    for (int i = 0; i < file_count; ++i) {
        if (!strcmp(files[i].path, path)) { selected = i; if (selected >= scroll + 8) scroll = selected - 7; if (selected < scroll) scroll = selected; load_preview(); return 0; }
    }
    return -1;
}

static int pc_receive_and_install(void) {
    char saved[512] = {0};
    int r = net_receive_one_vpk(DOWNLOAD_DIR, PC_INSTALL_PORT, saved, sizeof(saved), status_line, sizeof(status_line));
    if (r < 0) { snprintf(status_line, sizeof(status_line), "PC-overforing fel: 0x%08X", r); return r; }
    scan_vpks();
    if (select_path(saved) < 0) load_preview();
    r = install_selected();
    return r;
}


static int pc_receive_theme(void) {
    int r = net_receive_theme_zip(THEME_ZIP, PC_THEME_PORT, status_line, sizeof(status_line));
    if (r < 0) { snprintf(status_line,sizeof(status_line),"Tema-overforing fel: 0x%08X",r); return r; }
    rm_tree(THEME_DIR);
    sceIoMkdir(THEME_DIR, 0777);
    r = zip_extract_all(THEME_ZIP, THEME_DIR);
    sceIoRemove(THEME_ZIP);
    if (r < 0) { snprintf(status_line,sizeof(status_line),"Kunde inte packa upp tema: %d",r); return r; }
    load_custom_theme();
    vita2d_set_clear_color(theme.bg);
    snprintf(status_line,sizeof(status_line),"Custom theme installerat fran PC.");
    return 0;
}

static void draw_text(vita2d_pgf *font, int x, int y, unsigned int color, float scale, const char *text) {
    vita2d_pgf_draw_text(font, x, y, color, scale, text);
}

int main(void) {
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    vita2d_init();
    set_builtin_theme(0);
    vita2d_set_clear_color(theme.bg);
    vita2d_pgf *font = vita2d_load_default_pgf();
    ensure_dirs();
    /* Diagnostic startup mode: network and promoter are disabled temporarily
       so we can verify that the UI starts without C2-12828-1. */
    int net_res = -1;
    int promoter_res = -1;
    snprintf(status_line, sizeof(status_line), "Testlage: natverk/install avstangt.");
    refresh_vpk_list();

    SceCtrlData pad, oldpad;
    memset(&pad, 0, sizeof(pad)); memset(&oldpad, 0, sizeof(oldpad));

    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int pressed = pad.buttons & ~oldpad.buttons;
        int changed = 0;
        if (!settings_open && (pressed & SCE_CTRL_UP)) {
            if (selected > 0) { selected--; changed = 1; }
            if (selected < scroll) scroll = selected;
        }
        if (!settings_open && (pressed & SCE_CTRL_DOWN)) {
            if (selected + 1 < file_count) { selected++; changed = 1; }
            if (selected >= scroll + 8) scroll = selected - 7;
        }
        if (changed) load_preview();
        if (!settings_open && (pressed & SCE_CTRL_CROSS)) snprintf(status_line, sizeof(status_line), "Install avstangt i testlage.");
        if (!settings_open && (pressed & SCE_CTRL_RTRIGGER)) snprintf(status_line, sizeof(status_line), "PC-VPK avstangt i testlage.");
        if (!settings_open && (pressed & SCE_CTRL_LTRIGGER)) snprintf(status_line, sizeof(status_line), "PC-Tema avstangt i testlage.");
        if (!settings_open && (pressed & SCE_CTRL_TRIANGLE)) delete_selected();
        if (!settings_open && (pressed & SCE_CTRL_SQUARE)) refresh_vpk_list();
        if (pressed & SCE_CTRL_START) settings_open = !settings_open;
        if (settings_open) {
            if (pressed & SCE_CTRL_UP) { theme_choice--; if (theme_choice < 0) theme_choice = 3; }
            if (pressed & SCE_CTRL_DOWN) { theme_choice++; if (theme_choice > 3) theme_choice = 0; }
            if (pressed & SCE_CTRL_CROSS) { apply_theme_choice(theme_choice); settings_open = 0; }
            if (pressed & SCE_CTRL_CIRCLE) settings_open = 0;
        } else if (pressed & SCE_CTRL_CIRCLE) break;

        vita2d_start_drawing(); vita2d_clear_screen();
        if (theme_bg) {
            float sx = 960.0f / vita2d_texture_get_width(theme_bg);
            float sy = 544.0f / vita2d_texture_get_height(theme_bg);
            vita2d_draw_texture_scale(theme_bg, 0, 0, sx, sy);
        }
        draw_text(font, 28, 42, theme.accent, 1.15f, "VPK Manager v5");
        draw_text(font, 28, 70, RGBA8(190,190,190,255), 0.72f, "ux0:/download/");
        char pcinfo[160]; snprintf(pcinfo, sizeof(pcinfo), "PC: VPK %s:%d (R)  Theme %s:%d (L)", vita_ip, PC_INSTALL_PORT, vita_ip, PC_THEME_PORT);
        draw_text(font, 350, 70, RGBA8(120,220,255,255), 0.62f, pcinfo);

        for (int i = 0; i < 8 && scroll + i < file_count; ++i) {
            int idx = scroll + i; int y = 112 + i * 48;
            if (idx == selected) vita2d_draw_rectangle(20, y - 28, 565, 42, theme.selected);
            char line[360]; double mb = (double)files[idx].size / (1024.0 * 1024.0);
            snprintf(line, sizeof(line), "%s  %.2f MB", files[idx].name, mb);
            draw_text(font, 30, y, theme.text, 0.69f, line);
        }
        if (file_count == 0) draw_text(font, 30, 130, RGBA8(220,220,220,255), 0.85f, "Inga .vpk-filer hittades.");

        vita2d_draw_rectangle(605, 84, 335, 370, theme.panel);
        if (preview_icon) {
            float sx = 128.0f / vita2d_texture_get_width(preview_icon);
            float sy = 128.0f / vita2d_texture_get_height(preview_icon);
            vita2d_draw_texture_scale(preview_icon, 708, 105, sx, sy);
        } else {
            vita2d_draw_rectangle(708, 105, 128, 128, RGBA8(50,50,55,255));
            draw_text(font, 731, 174, RGBA8(160,160,160,255), 0.65f, "Ingen ikon");
        }
        draw_text(font, 625, 270, theme.accent, 0.73f, meta.title[0] ? meta.title : "Okand app");
        char info[180];
        snprintf(info,sizeof(info),"Title ID: %s", meta.titleid[0] ? meta.titleid : "-"); draw_text(font,625,305,RGBA8(220,220,220,255),0.65f,info);
        snprintf(info,sizeof(info),"Version: %s", meta.version[0] ? meta.version : "-"); draw_text(font,625,335,RGBA8(220,220,220,255),0.65f,info);
        if (file_count > 0) { snprintf(info,sizeof(info),"Fil: %.2f MB",(double)files[selected].size/(1024.0*1024.0)); draw_text(font,625,365,RGBA8(220,220,220,255),0.65f,info); }

        draw_text(font, 28, 500, RGBA8(235,235,235,255), 0.64f, "X Installera  R PC-VPK  L PC-Tema  Triangle Ta bort  Square Uppdatera  START Tema  O Avsluta");
        if (settings_open) {
            vita2d_draw_rectangle(210, 85, 540, 365, RGBA8(5,5,8,235));
            draw_text(font, 245, 125, theme.accent, 1.0f, "Installningar > Tema");
            const char *opts[4] = {"Blue (Standard)", "Green", "Purple", "Custom"};
            for (int t=0;t<4;t++) {
                int yy=185+t*55;
                if (t==theme_choice) vita2d_draw_rectangle(235, yy-30, 490, 42, theme.selected);
                draw_text(font, 260, yy, theme.text, 0.82f, opts[t]);
            }
            draw_text(font, 245, 420, RGBA8(190,190,190,255), 0.62f, "X Valj   O Tillbaka   Custom: ux0:/data/vpk_manager/theme/");
        }
        draw_text(font, 28, 535, RGBA8(255,195,90,255), 0.64f, status_line);

        vita2d_end_drawing(); vita2d_swap_buffers(); oldpad = pad;
    }

    clear_preview(); rm_tree(INSTALL_DIR); free_theme_bg();
    /* net_receiver_term() and scePromoterUtilityExit() are disabled in test mode. */
    vita2d_free_pgf(font); vita2d_fini(); sceKernelExitProcess(0); return 0;
}
