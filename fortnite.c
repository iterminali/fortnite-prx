/* ============================================================
 *  fortnite.c — plugin GoldHEN (PS4)
 *  Notif système + NoRecoil + SpeedFire
 *  Build : voir meson.build / cross file du GoldHEN_Plugins_SDK
 * ============================================================ */

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <orbis/libkernel.h>
#include "khook.h"

#define TRAINER_NAME    "Fortnite Trainer"
#define TRAINER_VERSION "v1.0"
#define TITLE_ID        "CUSA07022"

/* ---------- notifications système (toast GoldHEN-like) ---------- */

typedef struct {
    char reserved[45];
    char message[3075];
} notify_request_t;

static void notify(const char *fmt, ...)
{
    notify_request_t req;
    va_list args;

    memset(&req, 0, sizeof(req));
    va_start(args, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, args);
    va_end(args);

    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

/* ---------- recherche de module / scan de pattern ---------- */

static uintptr_t g_eboot_base = 0;
static size_t    g_eboot_size = 0;

static int resolve_eboot(void)
{
    size_t count = 0;
    size_t i;

    if (sceKernelGetModuleList(0xFF, NULL, &count) != 0 || count == 0)
        return -1;

    OrbisKernelModule *list = __builtin_alloca(count * sizeof(OrbisKernelModule));
    if (sceKernelGetModuleList(0xFF, list, &count) != 0)
        return -1;

    for (i = 0; i < count; i++) {
        if (strstr(list[i].name, "eboot.bin")) {
            g_eboot_base = (uintptr_t)list[i].segment[0].addr;
            g_eboot_size = (size_t)list[i].segment[0].size;
            return 0;
        }
    }
    return -1;
}

/* pattern type "F3 0F 11 8B ?? ?? ?? ?? C3" ("??" = wildcard) */
static uint8_t *find_pattern(uint8_t *start, size_t len, const char *pattern)
{
    static uint8_t bytes[64];
    static uint8_t mask[64];
    size_t plen = 0;
    size_t i;
    const char *p = pattern;

    while (*p && plen < sizeof(bytes)) {
        while (*p == ' ') p++;
        if (!*p) break;

        if (p[0] == '?' && p[1] == '?') {
            bytes[plen] = 0x00;
            mask[plen]  = 0x00;
            plen++;
            p += 2;
        } else {
            unsigned int v;
            if (sscanf(p, "%2x", &v) != 1) return NULL;
            bytes[plen] = (uint8_t)v;
            mask[plen]  = 0xFF;
            plen++;
            p += 2;
        }
    }
    if (plen == 0 || plen > len) return NULL;

    for (i = 0; i <= len - plen; i++) {
        size_t j;
        for (j = 0; j < plen; j++) {
            if ((start[i + j] & mask[j]) != (bytes[j] & mask[j]))
                break;
        }
        if (j == plen)
            return &start[i];
    }
    return NULL;
}

/* ---------- patchs ---------- */

typedef struct {
    const char    *label;
    const char    *pattern;
    int            offset;   /* offset depuis le début du match */
    const uint8_t *bytes;    /* octets de remplacement */
    uint8_t        len;
    uint8_t       *addr;
    uint8_t        backup[16];
} patch_t;

/* NOP x5 : neutralise le call d'application du recul */
static const uint8_t NOP5[5] = { 0x90, 0x90, 0x90, 0x90, 0x90 };

static patch_t g_patches[] = {
    {
        /* call vers la fonction qui applique le recul (pitch/yaw accumulés) */
        .label   = "NoRecoil",
        .pattern = "E8 ?? ?? ?? ?? 48 8B 8B ?? ?? ?? ?? F3 0F 10 87",
        .offset  = 0,
        .bytes   = NOP5,
        .len     = 5,
    },
};

#define PATCH_COUNT (sizeof(g_patches) / sizeof(g_patches[0]))

/* ---------- speedfire (hook du getter de fire rate) ---------- */

static float (*orig_get_fire_rate)(void *self) = NULL;
static uint8_t *g_fire_rate_addr = NULL;
static const float SPEEDFIRE_VALUE = 0.01f; /* cadence max, ajuste si trop violent */

static float hook_get_fire_rate(void *self)
{
    (void)self;
    return SPEEDFIRE_VALUE;
}

/* ---------- entry points ---------- */

static int wait_for_eboot(void)
{
    int tries;

    for (tries = 0; tries < 100; tries++) {      /* ~10 s max */
        if (resolve_eboot() == 0 && g_eboot_size > 0)
            return 0;
        sceKernelUsleep(100 * 1000);
    }
    return -1;
}

int plugin_load(void *handle)
{
    size_t i;
    int applied = 0;

    (void)handle;

    notify("%s %s : chargement...", TRAINER_NAME, TRAINER_VERSION);

    if (wait_for_eboot() != 0) {
        notify("%s : eboot.bin introuvable (mauvais jeu ?)", TRAINER_NAME);
        return -1;
    }

    /* NoRecoil */
    for (i = 0; i < PATCH_COUNT; i++) {
        patch_t *pt = &g_patches[i];
        uint8_t *hit = find_pattern((uint8_t *)g_eboot_base, g_eboot_size, pt->pattern);

        if (!hit) {
            notify("%s : pattern %s introuvable (MAJ du jeu ?)", TRAINER_NAME, pt->label);
            continue;
        }

        pt->addr = hit + pt->offset;
        memcpy(pt->backup, pt->addr, pt->len);
        memcpy(pt->addr, pt->bytes, pt->len);
        applied++;
    }

    /* SpeedFire */
    g_fire_rate_addr = find_pattern((uint8_t *)g_eboot_base, g_eboot_size,
                                    "F3 0F 10 87 ?? ?? ?? ?? C3");
    if (g_fire_rate_addr) {
        hook_call((uintptr_t)g_fire_rate_addr,
                  (void *)hook_get_fire_rate,
                  (void **)&orig_get_fire_rate);
        applied++;
    } else {
        notify("%s : getter de fire rate introuvable", TRAINER_NAME);
    }

    if (applied > 0)
        notify("%s actif : NoRecoil + SpeedFire (x%d)", TRAINER_NAME, applied);
    else
        notify("%s : aucun patch appliqué", TRAINER_NAME);

    return 0;
}

int plugin_unload(void *handle)
{
    size_t i;

    (void)handle;

    if (g_fire_rate_addr) {
        kunhook_call((uintptr_t)g_fire_rate_addr);
        g_fire_rate_addr = NULL;
    }

    for (i = 0; i < PATCH_COUNT; i++) {
        patch_t *pt = &g_patches[i];
        if (pt->addr) {
            memcpy(pt->addr, pt->backup, pt->len);
            pt->addr = NULL;
        }
    }

    notify("%s desactive", TRAINER_NAME);
    return 0;
}
