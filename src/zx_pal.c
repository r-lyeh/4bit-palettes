// extracts palettes from Spectral zx emulator
// generates jasc .pal files
// generates ula+ .tap palettes
// generates mosaic png for each palette, using the top100 voted artwork from zxart.ee (in random order)
// - rlyeh, public domain

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#ifdef _MSC_VER
#include "dirent.h"
#define strcasecmp strcmpi
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define W 320
#define H 240

typedef struct { unsigned char r, g, b; } RGB;
typedef struct {
    char name[256];
    char story[256];
    RGB colors[16];
} Palette;
typedef struct {
    char *name;
    RGB *pixels;
} Source;

static const RGB spectrum[16] = {
    {0,0,0},{0,0,205},{205,0,0},{205,0,205},
    {0,205,0},{0,205,205},{205,205,0},{205,205,205},
    {0,0,0},{0,0,255},{255,0,0},{255,0,255},
    {0,255,0},{0,255,255},{255,255,0},{255,255,255}
};

// Official tokenized BASIC prefix supplied for the 64colour loader.
// It includes the 4-byte BASIC line header.
static const unsigned char basic_prefix[48] = {
    0x00, 0x00, 0x93, 0x00,
    0xF9, 0xC0, 0x28, 0x28, 0xBE, 0xB0,
    0x22, 0x32, 0x33, 0x36, 0x33, 0x35, 0x22,
    0x2B, 0xB0, 0x22, 0x32, 0x35, 0x36, 0x22,
    0x2A, 0xBE, 0xB0, 0x22, 0x32, 0x33, 0x36,
    0x33, 0x36, 0x22, 0x29, 0x2B, 0xB0, 0x22,
    0x34, 0x38, 0x22, 0x29, 0x3A, 0xEF, 0x22,
    0x22, 0x3A, 0xEA,
};

// Official 38-byte Z80 routine.
static const unsigned char z80_loader[38] = {
    0xF3,                    // DI
    0x21, 0x26, 0x00,        // LD HL,38
    0x09,                    // ADD HL,BC
    0x01, 0x3B, 0xBF,        // LD BC,$BF3B
    0x3E, 0x40,              // LD A,64
    0xED, 0x79,              // OUT (C),A
    0x3E, 0x01,              // LD A,1
    0x06, 0xFF,              // LD B,$FF
    0xED, 0x79,              // OUT (C),A
    0xAF,                    // XOR A
    0x06, 0xBF,              // setreg: LD B,$BF
    0xED, 0x79,              // OUT (C),A
    0x08,                    // EX AF,AF'
    0x7E,                    // LD A,(HL)
    0x06, 0xFF,              // LD B,$FF
    0xED, 0x79,              // OUT (C),A
    0x08,                    // EX AF,AF'
    0x23,                    // INC HL
    0x3C,                    // INC A
    0xFE, 0x40,              // CP 64
    0x20, 0xEF,              // JR NZ,setreg
    0xFB,                    // EI
    0xC9,                    // RET
};

static void safe_name(const char *in, char *out, size_t cap) {
    size_t n = 0;
    for (; *in && n + 1 < cap; ++in) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c) || c == '_' || c == '-' || c == '.')
            out[n++] = (char)c;
        else if (n && out[n-1] != '_')
            out[n++] = '_';
    }
    while (n && (out[n-1] == '.' || out[n-1] == '_')) --n;
    if (!n && cap > 1) out[n++] = 'p';
    out[n] = '\0';
}

static int mkdir_p(const char *path) {
    char tmp[1024];
    size_t n = strlen(path);
    if (n >= sizeof tmp) return -1;
    memcpy(tmp, path, n + 1);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    return mkdir(tmp, 0777) == 0 || 1;
}

static int rgb_distance(RGB a, RGB b) {
    int r = a.r-b.r, g = a.g-b.g, bl = a.b-b.b;
    return r*r + g*g + bl*bl;
}

static RGB *render(const RGB *src, const Palette *p, int count) {
    RGB *dst = malloc((size_t)count * sizeof *dst);
    if (!dst) return NULL;
    for (int k = 0; k < count; ++k) {
        int best = 0, dmin = rgb_distance(src[k], spectrum[0]);
        for (int i = 1; i < 16; ++i) {
            int d = rgb_distance(src[k], spectrum[i]);
            if (d < dmin) best = i, dmin = d;
        }
        dst[k] = p->colors[best];
    }
    return dst;
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char *s = malloc((size_t)len + 1);
    if (!s) { fclose(f); return NULL; }
    if (fread(s, 1, (size_t)len, f) != (size_t)len) {
        free(s); fclose(f); return NULL;
    }
    s[len] = '\0';
    fclose(f);
    return s;
}

#include <stdbool.h>
#define countof(x) ( sizeof(x) / sizeof(0[x]))
#define byte unsigned char
#define rgba unsigned
#define rgb(r,g,b) ((((int)(r))<<0)|(((int)(g))<<8)|(((int)(b))<<16)|255<<24)
#include "spectral/zx_pal.h"
#define NUM_PALETTES (ZX_PALETTE_EXTERNAL-1)

static int load_palettes(Palette **out) {
    int count = NUM_PALETTES;
    Palette *items = realloc(NULL, count * sizeof *items);

    for (int p = 0; p < count; ++p )
    {
        Palette pal = {0};

        for( int i = 0; i < 16; ++i )
            pal.colors[i].r = (ZXPalettes[p][i] >>  0) & 255,
            pal.colors[i].g = (ZXPalettes[p][i] >>  8) & 255,
            pal.colors[i].b = (ZXPalettes[p][i] >> 16) & 255;

        snprintf(pal.name, sizeof pal.name, "%s", ZXPaletteNames[p] + 2);

        // name patches
        for(int i = 0; pal.name[i]; ++i) { if(pal.name[i] == '+') pal.name[i] = '2'; if(pal.name[i] < 32) pal.name[i] = 0; }
        if(!strcmp(pal.name, "II"))
            snprintf(pal.name, sizeof pal.name, "Quirkafleeg2");
        if(!strncmp(pal.name, "Lumi", 4))
            snprintf(pal.name, sizeof pal.name, "Lumiere");

        items[p] = pal;
    }

    *out = items;
    return count;
}

static uint8_t ula(RGB c) {
    return (uint8_t)((c.g & 0xE0) | ((c.r >> 3) & 0x1C) | (c.b >> 6));
}

static void put16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

static size_t tap_block(const unsigned char *payload, size_t n,
                        unsigned char *out) {
    put16(out, (unsigned)(n + 1));
    memcpy(out + 2, payload, n);
    unsigned char x = 0;
    for (size_t i = 0; i < n; ++i) x ^= payload[i];
    out[n+2] = x;
    return n + 3;
}

static int write_tap(const char *path, const Palette *p) {
    /* These arrays must be filled with the Python constants. */
    if (sizeof basic_prefix != 48 || sizeof z80_loader != 38) return 0;

    unsigned char program[150];
    memcpy(program, basic_prefix, 48);
    memcpy(program+48, z80_loader, 38);
    for (int i = 0; i < 16; ++i) program[86+i] = ula(p->colors[i]);
    memset(program+102, 0, 48);

    unsigned char header[19] = {0,0, 0x14,0x01,'6','4','c','o','l','o','u','r'};
    put16(header+12, sizeof program);
    put16(header+14, 0);
    put16(header+16, sizeof program);

    unsigned char file[256];
    size_t n = tap_block(header, 18, file);
    unsigned char flag_program[151];
    flag_program[0] = 0xFF;
    memcpy(flag_program+1, program, sizeof program);
    n += tap_block(flag_program, sizeof flag_program, file+n);

    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(file, 1, n, f) == n;
    fclose(f);
    return ok;
}

int main(int argc, char **argv) {
    const char *input = argc > 1 ? argv[1] : "input";
    const char *output = argc > 2 ? argv[2] : "..";

    Palette *palettes = NULL;
    int np = load_palettes(&palettes);
    if (!np) { fprintf(stderr, "Could not parse palettes\n"); return 1; }

    if(output[0] != '.') mkdir_p(output);
    DIR *dir = opendir(input);
    if (!dir) { perror(input); free(palettes); return 1; }

    Source *images = NULL;
    int ni = 0;
    struct dirent *de;
    while ((de = readdir(dir))) {
        size_t len = strlen(de->d_name);
        if (len < 4 || strcasecmp(de->d_name + len - 4, ".png")) continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", input, de->d_name);
        puts(path);
        int w, h, channels;
        unsigned char *raw = stbi_load(path, &w, &h, &channels, 3);
        if (!raw || w != W || h != H) { stbi_image_free(raw); continue; }
        Source *grown = realloc(images, (size_t)(ni+1)*sizeof *images);
        if (!grown) { stbi_image_free(raw); break; }
        images = grown;
        images[ni].name = strdup(de->d_name);
        images[ni].pixels = (RGB *)raw;
        ++ni;
    }
    closedir(dir);
    if (!ni) { fprintf(stderr, "No valid 320x240 PNGs found\n"); return 1; }

    FILE *README = fopen("../README.md", "wt");
    fprintf(README, "# Palettes\r\n");
    fprintf(README, "4-bit palettes for the ZX Sinclair Spectrum computer. Extracted from [Spectral emulator](https://github.com/r-lyeh/Spectral).\r\n\r\n");

    fprintf(README, "Each palette entry contains:\r\n- A JASC 0100 .pal file,\r\n- A compatible ZX Spectrum .tap file (ULA+)\r\n- Image collage using the top100 voted artwork from [zxart.ee website](https://zxart.ee) (in random order)\r\n\r\n");

    for (int pi = 0; pi < np; ++pi) {
        char name[256], subdir[1024], path[1200];
        safe_name(palettes[pi].name, name, sizeof name);
        snprintf(subdir, sizeof subdir, "%s/%s", output, name);
#if 0
        mkdir_p(subdir);
#endif
        puts(subdir);

        snprintf(path, sizeof path, "%s/%s.pal", output, name);
        FILE *pf = fopen(path, "w");
        if (pf) {
            fprintf(pf, "JASC-PAL\n0100\n16\n");
            for (int c = 0; c < 16; ++c)
                fprintf(pf, "%u %u %u\n", palettes[pi].colors[c].r,
                        palettes[pi].colors[c].g, palettes[pi].colors[c].b);
            fclose(pf);
        }

        snprintf(path, sizeof path, "%s/%s.tap", output, name);
        write_tap(path, &palettes[pi]);

        RGB *sheet = malloc((size_t)W * H * ni * sizeof *sheet);
        if (!sheet) continue;
        for (int i = 0; i < ni; ++i) {
            RGB *rendered = render(images[i].pixels, &palettes[pi], W*H);
            if (!rendered) continue;
            memcpy(sheet + (size_t)i*W*H, rendered, (size_t)W*H*sizeof *sheet);
#if 0
            snprintf(path, sizeof path, "%s/%03d.png", subdir, i);
            stbi_write_png(path, W, H, 3, rendered, W*3);
#endif
            free(rendered);
        }

#if 1 // palette preview
        {
            for( int i = 0; i < 16; ++i )
            {
                int offx = W/2-(8*16)/2+i*16; if(i>7) offx -= W/2, offx += 16*2;
                int offy = 16*(i>7);

                for( int y = 0; y < 16; ++y )
                for( int x = 0; x < 16; ++x )
                    sheet[ (offx+x) + (offy+y) * W ] = palettes[pi].colors[i];
            }

            {
                RGB white = {255,255,255};

                int offx = W/2-(8*16)/2;
                int offy = 0;

                memset(&sheet[offx+   0*W], 255, 16*8*3);
                memset(&sheet[offx+16*2*W], 255, 16*8*3);

                for( int y = 1; y < 32; ++y)
                    sheet[offx-1+y*W] = sheet[offx+16*8+y*W] = white;
            }
        }
#endif

#if 0
        snprintf(path, sizeof path, "%s/index.png", subdir);
        stbi_write_png(path, W, H*ni, 3, sheet, W*3);
#endif
        snprintf(path, sizeof path, "%s/%s.png", output, name);
        stbi_write_png(path, W, H*ni, 3, sheet, W*3);
        free(sheet);

        fprintf(README, "<img src=\"%s.png\" alt=\"%s\" width=16px/>", name, name);
    }

    fclose(README);

    for (int i = 0; i < ni; ++i) { free(images[i].name); free(images[i].pixels); }
    free(images);
    free(palettes);
    return 0;
}
