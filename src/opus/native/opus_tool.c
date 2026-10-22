#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

/* API de libopus: declarada aquí porque no conservamos opus.h */
typedef struct OpusEncoder OpusEncoder;

extern OpusEncoder *opus_encoder_create(
    int32_t, int, int, int *);
extern void opus_encoder_destroy(OpusEncoder *);
extern int opus_encode(
    OpusEncoder *, const int16_t *, int, unsigned char *, int);
extern int opus_encoder_ctl(OpusEncoder *, int, ...);

#define OPUS_APPLICATION_AUDIO 2049
#define OPUS_SET_BITRATE_REQUEST 4002
#define OPUS_GET_LOOKAHEAD_REQUEST 4027

#define RATE 48000
#define FRAME 960
#define MAX_PACKET 4000

static uint32_t serial = 0x49414E31;
static uint32_t seqno = 0;

static void le16(unsigned char *p, uint16_t v) {
    p[0] = v & 255;
    p[1] = (v >> 8) & 255;
}

static void le32(unsigned char *p, uint32_t v) {
    p[0] = v & 255;
    p[1] = (v >> 8) & 255;
    p[2] = (v >> 16) & 255;
    p[3] = (v >> 24) & 255;
}

static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t get16(const unsigned char *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t ogg_crc(const unsigned char *p, size_t n) {
    uint32_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint32_t)p[i] << 24;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80000000)
                ? (crc << 1) ^ 0x04C11DB7
                : crc << 1;
    }
    return crc;
}

/* Escribe una página Ogg con un paquete completo. */
static int ogg_page(FILE *f, const unsigned char *packet, size_t n,
                    uint64_t granule, int flags) {
    if (n > 65025) return 0;

    unsigned char page[65300];
    size_t segments = n / 255 + 1;
    size_t header = 27 + segments;

    memcpy(page, "OggS", 4);
    page[4] = 0;
    page[5] = flags;

    for (int i = 0; i < 8; i++)
        page[6 + i] = (unsigned char)(granule >> (8 * i));

    le32(page + 14, serial);
    le32(page + 18, seqno++);
    le32(page + 22, 0);
    page[26] = (unsigned char)segments;

    size_t remaining = n;
    for (size_t i = 0; i < segments; i++) {
        unsigned int l = remaining >= 255 ? 255 : (unsigned int)remaining;
        page[27 + i] = (unsigned char)l;
        remaining -= l;
    }

    if (n) memcpy(page + header, packet, n);

    size_t total = header + n;
    le32(page + 22, ogg_crc(page, total));

    return fwrite(page, 1, total, f) == total;
}

static int write_opus_headers(FILE *out, int preskip) {
    unsigned char head[19] = {0};
    memcpy(head, "OpusHead", 8);
    head[8] = 1;
    head[9] = 1;
    le16(head + 10, (uint16_t)preskip);
    le32(head + 12, RATE);
    le16(head + 16, 0);
    head[18] = 0;

    if (!ogg_page(out, head, sizeof(head), 0, 2))
        return 0;

    unsigned char tags[64] = {0};
    const char *vendor = "Ian opus_tool";
    size_t vl = strlen(vendor);
    memcpy(tags, "OpusTags", 8);
    le32(tags + 8, (uint32_t)vl);
    memcpy(tags + 12, vendor, vl);
    le32(tags + 12 + vl, 0);

    return ogg_page(out, tags, 16 + vl, 0, 0);
}

static OpusEncoder *new_encoder(int *preskip) {
    int err = 0;
    OpusEncoder *enc = opus_encoder_create(
        RATE, 1, OPUS_APPLICATION_AUDIO, &err);

    if (!enc || err != 0) {
        fprintf(stderr, "No se pudo crear el codificador Opus (%d).\n", err);
        return NULL;
    }

    opus_encoder_ctl(enc, OPUS_SET_BITRATE_REQUEST, 64000);
    opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD_REQUEST, preskip);
    return enc;
}

/* PCM -> ORAW: formato propio original. */
static int pcm_to_oraw(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        perror(src);
        return 0;
    }

    FILE *out = fopen(dst, "wb");
    if (!out) {
        perror(dst);
        fclose(in);
        return 0;
    }

    unsigned char header[12] = {'O','P','U','S','R','A','W','1'};
    header[8] = 0; header[9] = 0; header[10] = 0xbb; header[11] = 0x80;

    int ok = fwrite(header, 1, 12, out) == 12;
    int preskip = 0;
    OpusEncoder *enc = new_encoder(&preskip);

    if (!enc) ok = 0;

    int16_t pcm[FRAME] = {0};
    unsigned char packet[MAX_PACKET];

    while (ok) {
        size_t n = fread(pcm, sizeof(int16_t), FRAME, in);
        if (!n) break;

        if (n < FRAME)
            memset(pcm + n, 0, (FRAME - n) * sizeof(int16_t));

        int bytes = opus_encode(enc, pcm, FRAME, packet, MAX_PACKET);
        if (bytes <= 0 || bytes > 65535) {
            fprintf(stderr, "Error codificando Opus.\n");
            ok = 0;
            break;
        }

        unsigned char len[2];
        le16(len, (uint16_t)bytes);

        if (fwrite(len, 1, 2, out) != 2 ||
            fwrite(packet, 1, (size_t)bytes, out) != (size_t)bytes) {
            ok = 0;
            break;
        }

        memset(pcm, 0, sizeof(pcm));
    }

    if (ferror(in)) ok = 0;
    if (enc) opus_encoder_destroy(enc);
    fclose(in);
    if (fclose(out) != 0) ok = 0;

    if (!ok) remove(dst);
    else printf("ORAW creado: %s\n", dst);

    return ok;
}

/* PCM u ORAW -> contenedor Ogg Opus estándar. */
static int encode_ogg(const char *src, const char *dst, int from_oraw) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        perror(src);
        return 0;
    }

    if (from_oraw) {
        unsigned char h[12];
        if (fread(h, 1, 12, in) != 12 ||
            memcmp(h, "OPUSRAW1", 8) != 0 ||
            (h[8] != 0 || h[9] != 0 || h[10] != 0xbb || h[11] != 0x80)) {
            fprintf(stderr, "ORAW inválido o frecuencia no compatible.\n");
            fclose(in);
            return 0;
        }
    }

    FILE *out = fopen(dst, "wb");
    if (!out) {
        perror(dst);
        fclose(in);
        return 0;
    }

    serial++;
    seqno = 0;

    int preskip = 0;
    OpusEncoder *enc = from_oraw ? NULL : new_encoder(&preskip);

    /* ORAW ya contiene paquetes Opus; se usa el pre-skip habitual. */
    if (from_oraw) preskip = 312;

    int ok = from_oraw || enc != NULL;
    if (ok) ok = write_opus_headers(out, preskip);

    int16_t pcm[FRAME] = {0};
    unsigned char packet[MAX_PACKET];
    uint64_t samples = 0;
    int wrote_audio = 0;

    while (ok) {
        int bytes = 0;
        size_t real_samples = FRAME;

        if (from_oraw) {
            unsigned char lb[2];
            size_t got = fread(lb, 1, 2, in);

            if (got == 0) break;
            if (got != 2) {
                ok = 0;
                break;
            }

            bytes = get16(lb);
            if (bytes <= 0 || bytes > MAX_PACKET ||
                fread(packet, 1, (size_t)bytes, in) != (size_t)bytes) {
                ok = 0;
                break;
            }
        } else {
            real_samples = fread(pcm, sizeof(int16_t), FRAME, in);
            if (!real_samples) break;

            if (real_samples < FRAME)
                memset(pcm + real_samples, 0,
                       (FRAME - real_samples) * sizeof(int16_t));

            bytes = opus_encode(enc, pcm, FRAME, packet, MAX_PACKET);
            if (bytes <= 0) {
                ok = 0;
                break;
            }
        }

        samples += from_oraw ? FRAME : real_samples;
        uint64_t granule = (uint64_t)preskip + samples;
        int eos = !from_oraw && feof(in);

        /* Un paquete por página Ogg. */
        if (!ogg_page(out, packet, (size_t)bytes, granule, eos ? 4 : 0)) {
            ok = 0;
            break;
        }

        wrote_audio = 1;
        memset(pcm, 0, sizeof(pcm));

        if (!from_oraw && real_samples < FRAME)
            break;
    }

    if (ferror(in)) ok = 0;

    /* Para un PCM vacío, se emite un paquete silencioso final. */
    if (ok && !wrote_audio && !from_oraw) {
        int bytes = opus_encode(enc, pcm, FRAME, packet, MAX_PACKET);
        if (bytes <= 0 ||
            !ogg_page(out, packet, (size_t)bytes, preskip, 4))
            ok = 0;
    }

    if (enc) opus_encoder_destroy(enc);
    fclose(in);
    if (fclose(out) != 0) ok = 0;

    if (!ok) {
        remove(dst);
        fprintf(stderr, "Conversión fallida; se eliminó el archivo incompleto.\n");
    } else {
        printf("Ogg Opus creado: %s\n", dst);
    }

    return ok;
}

int main(void) {
    char src[1024], dst[1024], choice[32];

    puts("==================================");
    puts("       OPUS TOOL - LIBOPUS.A");
    puts("==================================");
    puts("1. PCM  -> ORAW");
    puts("2. ORAW -> OPUS estándar (Ogg)");
    puts("3. PCM  -> OPUS estándar (Ogg)");
    printf("Elige una conversión (1-3): ");
    fflush(stdout);

    if (!fgets(choice, sizeof(choice), stdin))
        return 1;

    int option = atoi(choice);
    if (option < 1 || option > 3) {
        puts("Opción no válida.");
        return 1;
    }

    printf("Archivo de entrada: ");
    fflush(stdout);
    if (!fgets(src, sizeof(src), stdin)) return 1;
    src[strcspn(src, "\r\n")] = 0;

    printf("Archivo de salida: ");
    fflush(stdout);
    if (!fgets(dst, sizeof(dst), stdin)) return 1;
    dst[strcspn(dst, "\r\n")] = 0;

    if (!src[0] || !dst[0] || strcmp(src, dst) == 0) {
        puts("Nombres de archivo no válidos.");
        return 1;
    }

    if (option == 1)
        return pcm_to_oraw(src, dst) ? 0 : 1;

    return encode_ogg(src, dst, option == 2) ? 0 : 1;
}
