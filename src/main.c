#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>

#include <vita2d.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "ziputil.h"
#include "sfo.h"
#include "net_receiver.h"
#include "vpk_head.h"
#include "vita_png_fix.h"

#define DOWNLOAD_DIR "ux0:/downloads"
#define DATA_DIR "ux0:/data/vpk_manager"
#define PREVIEW_DIR DATA_DIR "/preview"
#define INSTALL_DIR DATA_DIR "/install"
#define THEME_DIR DATA_DIR "/theme"
#define THEME_CFG THEME_DIR "/theme.ini"
#define THEME_BG THEME_DIR "/background.png"
#define APP_BG "app0:app_background.png"
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
static int preview_load_pending = 0;
static int preview_load_delay = 0;

static int selected = 0;
static int scroll = 0;
static VpkMeta meta;
static vita2d_texture *preview_icon = NULL;
static int preview_icon_valid = 0;

static volatile int cover_worker_busy = 0;
static volatile int cover_worker_done = 0;
static volatile int cover_worker_result = 0;
static volatile unsigned int cover_request_serial = 0;
static unsigned int cover_worker_serial = 0;
static SceUID cover_thread_uid = -1;
static char cover_worker_vpk[512] = {0};
static char cover_worker_selected_path[512] = {0};
static char cover_worker_name[NAME_LEN] = {0};
static unsigned char cover_worker_pixels[128 * 128 * 4];
static VpkMeta cover_worker_meta;
static int cover_worker_has_icon = 0;

static char status_line[256] = "Klar.";
static char vita_ip[32] = "-";

/* Installation worker state. Heavy VPK work runs outside the UI thread so
   the menu keeps drawing smoothly while installation is in progress. */
static volatile int install_busy = 0;
static volatile int install_progress = 0;
static volatile int install_result = 0;
static SceUID install_thread_uid = -1;
static char install_path[512] = {0};
static char install_name[NAME_LEN] = {0};
static char install_title[128] = {0};

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

static void load_app_background(void) {
    free_theme_bg();
    theme_bg = vita2d_load_PNG_file(APP_BG);
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
    if (choice == 3) {
        load_custom_theme();
        if (!theme_bg) load_app_background();
    } else {
        set_builtin_theme(choice);
        load_app_background();
    }
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

    const char *paths[] = {
        "ux0:/downloads",
        "ux0:/downloads/",
        "ux0:downloads",
        "ux0:downloads/"
    };

    int opened_any = 0;
    int total_entries = 0;
    int last_read = 0;
    int last_open_error = 0;

    for (unsigned int p = 0; p < sizeof(paths) / sizeof(paths[0]); ++p) {
        SceUID d = sceIoDopen(paths[p]);
        if (d < 0) {
            last_open_error = d;
            continue;
        }

        opened_any = 1;

        while (file_count < MAX_FILES) {
            SceIoDirent ent;
            memset(&ent, 0, sizeof(ent));

            int rr = sceIoDread(d, &ent);
            last_read = rr;

            if (rr <= 0)
                break;

            total_entries++;

            ent.d_name[sizeof(ent.d_name) - 1] = '\0';

            if (!strcmp(ent.d_name, ".") || !strcmp(ent.d_name, ".."))
                continue;

            if (!ends_with_vpk(ent.d_name))
                continue;

            snprintf(files[file_count].name,
                     sizeof(files[file_count].name),
                     "%s", ent.d_name);

            /* Always normalize stored paths to Vita's standard ux0:/ form. */
            snprintf(files[file_count].path,
                     sizeof(files[file_count].path),
                     "ux0:/downloads/%s", ent.d_name);

            SceIoStat st;
            memset(&st, 0, sizeof(st));

            int sr = sceIoGetstat(files[file_count].path, &st);
            if (sr >= 0)
                files[file_count].size = st.st_size;
            else
                files[file_count].size = ent.d_stat.st_size;

            file_count++;
        }

        sceIoDclose(d);

        /* Stop after the first path variant that actually lists entries.
           This prevents duplicates if several variants map to the same folder. */
        if (total_entries > 0)
            break;
    }

    if (!opened_any) {
        snprintf(status_line, sizeof(status_line),
                 "Kan inte oppna downloads-mappen: 0x%08X", last_open_error);
    } else if (file_count == 0) {
        snprintf(status_line, sizeof(status_line),
                 "0 VPK. Poster:%d Dread:%d", total_entries, last_read);
    }

    if (selected >= file_count)
        selected = file_count > 0 ? file_count - 1 : 0;

    if (scroll > selected)
        scroll = selected;
}

static void clear_preview(void) {
    preview_icon_valid = 0;
    memset(&meta, 0, sizeof(meta));
}


static void load_vpk_metadata_from_sfo(const char *sfo_path,
                                       const char *fallback_name,
                                       VpkMeta *out) {
    memset(out, 0, sizeof(*out));

    if (sfo_get_string(sfo_path, "TITLE",
                       out->title, sizeof(out->title)) < 0 ||
        !out->title[0]) {
        snprintf(out->title, sizeof(out->title), "%s", fallback_name);
    }

    if (sfo_get_string(sfo_path, "TITLE_ID",
                       out->titleid, sizeof(out->titleid)) < 0 ||
        !out->titleid[0]) {
        char content_id[128] = {0};

        /*
         * Most Vita CONTENT_ID values contain the title id after the first
         * dash, e.g. EP9000-PCSF00001_00-...
         */
        if (sfo_get_string(sfo_path, "CONTENT_ID",
                           content_id, sizeof(content_id)) == 0) {
            const char *dash = strchr(content_id, '-');
            if (dash && dash[1]) {
                dash++;
                size_t n = 0;
                while (dash[n] &&
                       dash[n] != '_' &&
                       dash[n] != '-' &&
                       n + 1 < sizeof(out->titleid)) {
                    out->titleid[n] = dash[n];
                    n++;
                }
                out->titleid[n] = 0;
            }
        }
    }

    if (sfo_get_string(sfo_path, "APP_VER",
                       out->version, sizeof(out->version)) < 0 ||
        !out->version[0]) {
        /* Some homebrew SFOs use VERSION instead of APP_VER. */
        sfo_get_string(sfo_path, "VERSION",
                       out->version, sizeof(out->version));
    }

    out->valid = 1;
}

static int cover_worker(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    memset(&cover_worker_meta, 0, sizeof(cover_worker_meta));
    cover_worker_has_icon = 0;
    cover_worker_result = 0;

    sceIoMkdir(PREVIEW_DIR, 0777);

    char sfo_path[512];
    char icon_path[512];
    snprintf(sfo_path, sizeof(sfo_path), "%s/async_param.sfo", PREVIEW_DIR);
    snprintf(icon_path, sizeof(icon_path), "%s/async_icon.png", PREVIEW_DIR);
    sceIoRemove(sfo_path);
    sceIoRemove(icon_path);

    int sr = zip_extract_named(cover_worker_vpk, "sce_sys/param.sfo", sfo_path);
    if (sr == 0) {
        load_vpk_metadata_from_sfo(sfo_path, cover_worker_name,
                                   &cover_worker_meta);
    } else {
        snprintf(cover_worker_meta.title,
                 sizeof(cover_worker_meta.title),
                 "%s", cover_worker_name);
        cover_worker_meta.valid = 1;
    }

    int ir = zip_extract_named(cover_worker_vpk, "sce_sys/icon0.png", icon_path);
    if (ir == 0) {
        int dr = vita_decode_preview_rgba128(
            icon_path, cover_worker_pixels, sizeof(cover_worker_pixels));
        if (dr == 0)
            cover_worker_has_icon = 1;
        else
            cover_worker_result = dr;
    }

    sceIoRemove(sfo_path);
    sceIoRemove(icon_path);

    cover_worker_done = 1;
    cover_worker_busy = 0;
    return 0;
}

static void start_cover_worker(void) {
    if (cover_worker_busy || file_count <= 0 ||
        selected < 0 || selected >= file_count)
        return;

    snprintf(cover_worker_vpk, sizeof(cover_worker_vpk),
             "%s", files[selected].path);
    snprintf(cover_worker_selected_path, sizeof(cover_worker_selected_path),
             "%s", files[selected].path);
    snprintf(cover_worker_name, sizeof(cover_worker_name),
             "%s", files[selected].name);

    cover_worker_serial = cover_request_serial;
    cover_worker_done = 0;
    cover_worker_result = 0;
    cover_worker_has_icon = 0;
    cover_worker_busy = 1;

    cover_thread_uid = sceKernelCreateThread(
        "VPKM_COVER", cover_worker, 0x10000140, 0x18000, 0, 0, NULL);

    if (cover_thread_uid < 0) {
        cover_worker_busy = 0;
        return;
    }

    int r = sceKernelStartThread(cover_thread_uid, 0, NULL);
    if (r < 0) {
        sceKernelDeleteThread(cover_thread_uid);
        cover_thread_uid = -1;
        cover_worker_busy = 0;
    }
}

static void load_preview(void) {
    cover_request_serial++;
    preview_load_pending = 1;
    preview_load_delay = 2;

    memset(&meta, 0, sizeof(meta));
    if (file_count > 0 && selected >= 0 && selected < file_count) {
        snprintf(meta.title, sizeof(meta.title),
                 "%s", files[selected].name);
        meta.valid = 1;
    }
}

static void schedule_preview_load(void) {
    cover_request_serial++;
    preview_load_pending = 1;
    preview_load_delay = 2;

    /*
     * Keep the selected filename visible immediately, but clear stale
     * metadata until the new VPK's param.sfo has arrived.
     */
    memset(&meta, 0, sizeof(meta));
    if (file_count > 0 && selected >= 0 && selected < file_count) {
        snprintf(meta.title, sizeof(meta.title),
                 "%s", files[selected].name);
        meta.valid = 1;
    }
}

static void service_preview_load(void) {
    /* Reap finished worker. */
    if (!cover_worker_busy && cover_thread_uid >= 0) {
        sceKernelWaitThreadEnd(cover_thread_uid, NULL, NULL);
        sceKernelDeleteThread(cover_thread_uid);
        cover_thread_uid = -1;
    }

    /* Apply only if this result still belongs to the current selection. */
    if (cover_worker_done) {
        cover_worker_done = 0;

        if (file_count > 0 &&
            selected >= 0 && selected < file_count &&
            !strcmp(files[selected].path, cover_worker_selected_path)) {
            meta = cover_worker_meta;

            if (cover_worker_has_icon) {
                if (!preview_icon)
                    preview_icon = vita2d_create_empty_texture(128, 128);

                if (preview_icon) {
                    vita2d_wait_rendering_done();

                    unsigned char *dstp =
                        (unsigned char *)vita2d_texture_get_datap(preview_icon);
                    unsigned int dst_stride =
                        vita2d_texture_get_stride(preview_icon);

                    if (dstp) {
                        for (unsigned int y = 0; y < 128; ++y) {
                            memcpy(dstp + y * dst_stride,
                                   cover_worker_pixels + y * 128u * 4u,
                                   128u * 4u);
                        }
                        preview_icon_valid = 1;
                    }
                }
            } else {
                preview_icon_valid = 0;
            }
        }
    }

    if (!preview_load_pending || install_busy || settings_open)
        return;

    if (preview_load_delay > 0) {
        preview_load_delay--;
        return;
    }

    /* If an older cover is still decoding, keep scrolling responsive.
       As soon as it finishes, this pending request starts automatically. */
    if (cover_worker_busy)
        return;

    preview_load_pending = 0;
    start_cover_worker();
}

static void refresh_vpk_list(void) {
    int old_selected = selected;

    scan_vpks();

    if (file_count <= 0) {
        selected = 0;
        scroll = 0;
        clear_preview();
        /* Keep scan_vpks() diagnostic text so we can see whether the
           directory opened and whether sceIoDread returned entries. */
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
             "%d VPK-fil%s hittad%s i ux0:/downloads/",
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


static int load_sce_paf(void) {
    static unsigned int argp[] = {
        0x180000, 0xFFFFFFFF, 0xFFFFFFFF, 1, 0xFFFFFFFF, 0xFFFFFFFF
    };

    int result = -1;
    unsigned int buf[4];

    buf[0] = sizeof(buf);
    buf[1] = (unsigned int)&result;
    buf[2] = 0xFFFFFFFF;
    buf[3] = 0xFFFFFFFF;

    return sceSysmoduleLoadModuleInternalWithArg(
        SCE_SYSMODULE_INTERNAL_PAF,
        sizeof(argp),
        argp,
        (const SceSysmoduleOpt *)buf
    );
}

static int unload_sce_paf(void) {
    unsigned int buf = 0;
    return sceSysmoduleUnloadModuleInternalWithArg(
        SCE_SYSMODULE_INTERNAL_PAF, 0, NULL, (const SceSysmoduleOpt *)&buf
    );
}

static int promote_package_safe(const char *path) {
    int r;
    int paf_loaded = 0;
    int promoter_loaded = 0;
    int promoter_inited = 0;

    r = load_sce_paf();
    if (r < 0)
        return r;
    paf_loaded = 1;

    r = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    if (r < 0)
        goto cleanup;
    promoter_loaded = 1;

    r = scePromoterUtilityInit();
    if (r < 0)
        goto cleanup;
    promoter_inited = 1;

    r = scePromoterUtilityPromotePkgWithRif(path, 1);

cleanup:
    if (promoter_inited)
        scePromoterUtilityExit();

    if (promoter_loaded)
        sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);

    if (paf_loaded)
        unload_sce_paf();

    return r;
}

static void install_extract_progress(int percent, const char *entry, void *user) {
    (void)entry;
    (void)user;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    /* Extraction occupies the first 60 percent of the full install bar. */
    install_progress = 5 + (percent * 55) / 100;
    snprintf(status_line, sizeof(status_line),
             "Packar upp... %d%%", install_progress);
}

static int install_worker(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    int r;
    int fixed_pngs = 0;

    install_progress = 2;
    snprintf(status_line, sizeof(status_line), "Forbereder %s... 2%%", install_name);

    rm_tree(INSTALL_DIR);
    sceIoMkdir(INSTALL_DIR, 0777);

    install_progress = 5;
    r = zip_extract_all_progress(install_path, INSTALL_DIR,
                                 install_extract_progress, NULL);
    if (r < 0) {
        snprintf(status_line, sizeof(status_line), "Fel vid uppackning: %d", r);
        rm_tree(INSTALL_DIR);
        install_result = r;
        install_busy = 0;
        return r;
    }

    install_progress = 65;
    snprintf(status_line, sizeof(status_line), "Kontrollerar Vita PNG... 65%%");
    r = vita_fix_sce_sys_pngs(INSTALL_DIR, &fixed_pngs);
    if (r < 0) {
        snprintf(status_line, sizeof(status_line),
                 "PNG-fix misslyckades: %d", r);
        rm_tree(INSTALL_DIR);
        install_result = r;
        install_busy = 0;
        return r;
    }

    install_progress = 78;
    snprintf(status_line, sizeof(status_line),
             "Forbereder paket (%d PNG fixade)... 78%%", fixed_pngs);
    r = vpk_make_head_bin(INSTALL_DIR);
    if (r < 0) {
        snprintf(status_line, sizeof(status_line),
                 "Kunde inte skapa head.bin: %d", r);
        rm_tree(INSTALL_DIR);
        install_result = r;
        install_busy = 0;
        return r;
    }

    install_progress = 90;
    snprintf(status_line, sizeof(status_line), "Installerar %s... 90%%",
             install_title[0] ? install_title : install_name);

    /* Keep the proven promoter flow unchanged; only move it off the UI thread. */
    r = promote_package_safe(INSTALL_DIR);

    if (r >= 0) {
        install_progress = 100;
        snprintf(status_line, sizeof(status_line), "Installation klar. 100%%");
    } else {
        snprintf(status_line, sizeof(status_line),
                 "Installationen misslyckades: 0x%08X", r);
    }

    rm_tree(INSTALL_DIR);
    install_result = r;
    install_busy = 0;
    return r;
}

static int start_install_selected(void) {
    if (install_busy) return -2;
    if (file_count <= 0) return -1;

    snprintf(install_path, sizeof(install_path), "%s", files[selected].path);
    snprintf(install_name, sizeof(install_name), "%s", files[selected].name);
    snprintf(install_title, sizeof(install_title), "%s",
             meta.title[0] ? meta.title : files[selected].name);

    install_progress = 0;
    install_result = 0;
    install_busy = 1;
    snprintf(status_line, sizeof(status_line), "Startar installation... 0%%");

    install_thread_uid = sceKernelCreateThread(
        "VPKM_INSTALL", install_worker, 0x10000100, 0x20000, 0, 0, NULL);
    if (install_thread_uid < 0) {
        int r = install_thread_uid;
        install_busy = 0;
        snprintf(status_line, sizeof(status_line),
                 "Kunde inte starta installationstrad: 0x%08X", r);
        return r;
    }

    int r = sceKernelStartThread(install_thread_uid, 0, NULL);
    if (r < 0) {
        sceKernelDeleteThread(install_thread_uid);
        install_thread_uid = -1;
        install_busy = 0;
        snprintf(status_line, sizeof(status_line),
                 "Kunde inte starta installation: 0x%08X", r);
        return r;
    }

    return 0;
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
    r = start_install_selected();
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
    load_app_background();
    vita2d_pgf *font = vita2d_load_default_pgf();
    ensure_dirs();
    /* PC network remains disabled. Promoter modules are loaded only after X. */
    int net_res = -1;
    snprintf(status_line, sizeof(status_line), "Redo. X = installera vald VPK.");

    refresh_vpk_list();

    SceCtrlData pad, oldpad;
    memset(&pad, 0, sizeof(pad)); memset(&oldpad, 0, sizeof(oldpad));
    int nav_repeat_delay = 0;
    int nav_hold_frames = 0;


    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned int pressed = pad.buttons & ~oldpad.buttons;
        int changed = 0;

        int up_held = (pad.buttons & SCE_CTRL_UP) != 0;
        int down_held = (pad.buttons & SCE_CTRL_DOWN) != 0;
        int nav_step = 0;

        if (!settings_open && !install_busy) {
            if (pressed & SCE_CTRL_UP) {
                nav_step = -1;
                nav_repeat_delay = 8;
                nav_hold_frames = 0;
            } else if (pressed & SCE_CTRL_DOWN) {
                nav_step = 1;
                nav_repeat_delay = 8;
                nav_hold_frames = 0;
            } else if (up_held || down_held) {
                nav_hold_frames++;

                if (nav_repeat_delay > 0) {
                    nav_repeat_delay--;
                } else {
                    nav_step = up_held ? -1 : 1;

                    /*
                     * Accelerate while the button stays held:
                     * short hold = controlled,
                     * medium hold = fast,
                     * long hold = one item every frame.
                     */
                    if (nav_hold_frames < 18)
                        nav_repeat_delay = 2;
                    else if (nav_hold_frames < 40)
                        nav_repeat_delay = 1;
                    else
                        nav_repeat_delay = 0;
                }
            } else {
                nav_repeat_delay = 0;
                nav_hold_frames = 0;
            }

            if (nav_step < 0 && selected > 0) {
                selected--;
                changed = 1;
            } else if (nav_step > 0 && selected + 1 < file_count) {
                selected++;
                changed = 1;
            }

            if (selected < scroll) scroll = selected;
            if (selected >= scroll + 8) scroll = selected - 7;
        }
        if (changed) schedule_preview_load();
        if (!settings_open && !install_busy && (pressed & SCE_CTRL_CROSS)) {
            preview_load_pending = 0;
            start_install_selected();
        }
        if (!settings_open && !install_busy && (pressed & SCE_CTRL_RTRIGGER))
            snprintf(status_line, sizeof(status_line), "PC-VPK fortfarande avstangt for stabilitet.");
        if (!settings_open && !install_busy && (pressed & SCE_CTRL_LTRIGGER))
            snprintf(status_line, sizeof(status_line), "PC-Tema fortfarande avstangt for stabilitet.");
        if (!settings_open && !install_busy && (pressed & SCE_CTRL_TRIANGLE)) { preview_load_pending = 0; delete_selected(); }
        if (!settings_open && !install_busy && (pressed & SCE_CTRL_SQUARE)) { preview_load_pending = 0; refresh_vpk_list(); }
        if (!install_busy && (pressed & SCE_CTRL_START)) settings_open = !settings_open;
        if (settings_open) {
            if (pressed & SCE_CTRL_UP) { theme_choice--; if (theme_choice < 0) theme_choice = 3; }
            if (pressed & SCE_CTRL_DOWN) { theme_choice++; if (theme_choice > 3) theme_choice = 0; }
            if (pressed & SCE_CTRL_CROSS) { apply_theme_choice(theme_choice); settings_open = 0; }
            if (pressed & SCE_CTRL_CIRCLE) settings_open = 0;
        } else if (!install_busy && (pressed & SCE_CTRL_CIRCLE)) break;

        /* Reap the worker after it has finished. */
        if (!install_busy && install_thread_uid >= 0) {
            sceKernelWaitThreadEnd(install_thread_uid, NULL, NULL);
            sceKernelDeleteThread(install_thread_uid);
            install_thread_uid = -1;
        }

        service_preview_load();

        vita2d_start_drawing(); vita2d_clear_screen();
        if (theme_bg) {
            float sx = 960.0f / vita2d_texture_get_width(theme_bg);
            float sy = 544.0f / vita2d_texture_get_height(theme_bg);
            vita2d_draw_texture_scale(theme_bg, 0, 0, sx, sy);
        }
        draw_text(font, 28, 42, theme.accent, 1.15f, "VPK Manager v5");
        draw_text(font, 28, 70, RGBA8(190,190,190,255), 0.72f, "ux0:/downloads/");
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
        if (preview_icon && preview_icon_valid) {
            float sx = 128.0f / vita2d_texture_get_width(preview_icon);
            float sy = 128.0f / vita2d_texture_get_height(preview_icon);
            vita2d_draw_texture_scale(preview_icon, 708, 105, sx, sy);
        } else {
            vita2d_draw_rectangle(708, 105, 128, 128, RGBA8(50,50,55,255));
            if (cover_worker_busy || preview_load_pending)
                draw_text(font, 724, 174, RGBA8(160,160,160,255), 0.65f, "Laddar...");
            else
                draw_text(font, 731, 174, RGBA8(160,160,160,255), 0.65f, "Ingen ikon");
        }
        draw_text(font, 625, 270, theme.accent, 0.73f, meta.title[0] ? meta.title : "Okand app");
        char info[180];
        snprintf(info,sizeof(info),"Title ID: %s",
                 meta.titleid[0] ? meta.titleid :
                 ((cover_worker_busy || preview_load_pending) ? "Laddar..." : "-"));
        draw_text(font,625,305,RGBA8(220,220,220,255),0.65f,info);

        snprintf(info,sizeof(info),"Version: %s",
                 meta.version[0] ? meta.version :
                 ((cover_worker_busy || preview_load_pending) ? "Laddar..." : "-"));
        draw_text(font,625,335,RGBA8(220,220,220,255),0.65f,info);
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
        if (install_busy) {
            int p = install_progress;
            if (p < 0) p = 0;
            if (p > 100) p = 100;

            /* Installation modal + animated progress bar. */
            vita2d_draw_rectangle(150, 165, 660, 190, RGBA8(5,5,8,238));
            draw_text(font, 185, 210, theme.accent, 1.0f, "Installerar VPK");

            char pct[32];
            snprintf(pct, sizeof(pct), "%d%%", p);
            draw_text(font, 710, 210, theme.text, 0.90f, pct);

            vita2d_draw_rectangle(185, 245, 590, 32, RGBA8(45,45,50,255));
            if (p > 0)
                vita2d_draw_rectangle(189, 249, (582.0f * p) / 100.0f, 24, theme.accent);

            draw_text(font, 185, 315, RGBA8(220,220,220,255), 0.67f, status_line);
            draw_text(font, 185, 340, RGBA8(150,150,155,255), 0.56f,
                      "Vanta tills installationen ar klar. Menyn fortsatter att uppdateras.");
        }

        draw_text(font, 28, 535, RGBA8(255,195,90,255), 0.64f, status_line);

        vita2d_end_drawing(); vita2d_swap_buffers(); oldpad = pad;
    }

    if (install_thread_uid >= 0) {
        sceKernelWaitThreadEnd(install_thread_uid, NULL, NULL);
        sceKernelDeleteThread(install_thread_uid);
        install_thread_uid = -1;
    }

    clear_preview();
    if (cover_thread_uid >= 0) {
        sceKernelWaitThreadEnd(cover_thread_uid, NULL, NULL);
        sceKernelDeleteThread(cover_thread_uid);
        cover_thread_uid = -1;
    }
    if (preview_icon) {
        vita2d_wait_rendering_done();
        vita2d_free_texture(preview_icon);
        preview_icon = NULL;
    }
    rm_tree(INSTALL_DIR);
    free_theme_bg();


    /* PC network remains disabled in this build. */
    vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
