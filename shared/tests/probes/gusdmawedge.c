/*
 * gusdmawedge.c -- standalone GF1 DMA-upload-vs-active-voice wedge probe
 * (GUS Campaign 3 follow-up, sdl-dos-ports S5 GUS PCM streaming research).
 *
 * Pure DJGPP; NO SDL, NO engine, NO C++. Sibling of gustone.c/gusdet.c/
 * gusdump.c -- same style, same safety discipline.
 *
 * ============================ WHY THIS EXISTS =============================
 * doskutsu's own real-hardware GUS campaign (memory/gus_campaign_picogus_
 * diag_wedge.md; GUS-PROBES.md's own "hard safety rule") found the real
 * PicoGUS WEDGES (hangs, needs a physical power cycle) when a GF1 port-READ
 * happens concurrent with an active voice -- confirmed 4 separate ways, e.g.
 * "Looping test-tone voice active during a .pat poke (gus-3)". The shipped
 * SDL3-DOS GF1 driver (vendor/SDL/src/audio/dos/SDL_dosaudio_gus.c) even
 * says so in its own comment on the one-shot test tone: "a forever-looping
 * voice keeps reading DRAM and contends with the engine's gameplay .pat PIO
 * poke -> wedge on the PicoGUS."
 *
 * That driver ALSO has an opt-in DMA-upload path (gus_upload_dma(),
 * SDL_HINT_DOS_GUS_DMA_UPLOAD=1, default OFF) whose own completion-wait loop
 * polls the HOST 8237 status port first, but falls through to reading a GF1
 * register (GUS_REG_DMA_CTRL bit 0x40, via gus_r8) if the 8237 hasn't
 * signalled yet -- i.e. it can still do a GF1 port-READ while the transfer
 * (and any other active voice) is in flight. That DMA path has NEVER been
 * validated on real hardware (the driver's own comment: "Promote to default
 * once g2k-validated" -- never done). Whether DMA upload dodges the PIO wedge
 * or hits the exact same one is UNKNOWN. This probe settles that empirically,
 * the same way GUSTONE settled the 22.05kHz dead-zone question.
 *
 * ============================== DESIGN =====================================
 * SAFE mode (default): bring up the GF1, upload + start a LOOPING voice 0
 * tone (safe -- upload-before-start, no concurrent voice), let it play for
 * DUR ms, stop it, exit. Exercises every stage of the harness except the
 * risky one, so the probe itself (and its build) can be validated with zero
 * hang risk.
 *
 * DMATEST=1 (the actual experiment, real-hardware-only, explicit opt-in):
 * after voice 0 is looping (confirmed active), attempt ONE chunked DMA
 * upload of a second short sample to a different DRAM region, mirroring the
 * real driver's gus_upload_dma() register sequence (8237 program -> GF1
 * DMA_ADDR/DMA_CTRL -> poll 8237 status THEN GF1 DMA_CTRL bit0x40) while
 * voice 0 keeps sounding. The log line immediately before that attempt is
 * the one to watch:
 *
 * OVERWRITETEST=1 (a second, distinct experiment added alongside DMATEST --
 * confirms SDL_DOSGusOverwriteSample's own real-hardware safety, sdl3-dos
 * patch 0139): DMATEST's own REPEAT loop already writes to the SAME fixed
 * address every attempt, but every one of those writes happens AFTER voice 0
 * is already active -- it never establishes "written once, safely, before
 * any voice existed" as its own baseline. That is the actual shape
 * SDL_DOSGusOverwriteSample exists for (a double-buffered stream's own
 * half-buffer: allocated ONCE via UploadSample before playback starts,
 * REFILLED in place many times after). OVERWRITETEST writes a second region
 * ("region B", 0x20000, clear of both voice 0's own tone at 0x0 and
 * DMATEST's region at 0x10000) once during bring-up -- before voice 0
 * starts, the same safe window every other DRAM write in this probe already
 * uses -- then, once voice 0 is looping, overwrites region B REPEAT times
 * while voice 0 (which never reads region B) keeps sounding. Same risky-
 * attempt-announcement discipline as DMATEST: the log line immediately
 * before each overwrite is the one to watch, and a survived run's final
 * post-hoc verify confirms the LAST overwrite's data actually landed (not
 * stale content from the safe pre-voice write).
 *
 *     gusdmawedge: ABOUT TO ATTEMPT DMA UPLOAD WHILE VOICE 0 IS ACTIVE
 *
 * - If the machine WEDGES (hangs, no further output, needs a power cycle),
 *   that line is the LAST one in GUSDMAWEDGE.LOG -- confirms DMA upload
 *   shares PIO's wedge risk. Do not build any streaming design on it.
 * - If a line reporting the DMA attempt's own outcome appears next
 *   ("DMA upload attempt RETURNED ..."), the machine survived the attempt
 *   itself -- refutes the immediate-hang hypothesis for this one attempt
 *   (repeat a few times / at different chunk sizes before trusting it
 *   generally; one clean pass is evidence, not proof, same as every other
 *   finding in this campaign).
 * - Either way, voice 0 is stopped and the second sample's DRAM contents are
 *   verified in a SAFE, bounded, post-hoc, no-active-voice read afterward
 *   (mirrors GUSDUMP's own NOINIT-safety framing) -- this tells you whether
 *   a *survived* attempt actually transferred correct data, or silently
 *   corrupted it.
 *
 * ============================== USAGE =======================================
 *   GUSDMAWEDGE                SAFE mode -- single looping tone, no DMA attempt
 *   GUSDMAWEDGE DMATEST=1      the actual experiment (see above). REAL-HARDWARE
 *                              RISK: may hang the machine; operator go-ahead
 *                              required before running this on the rig.
 *   GUSDMAWEDGE DMATEST=1 REPEAT=3   repeat the concurrent-DMA attempt N times
 *                              in the same voice-0-active window (a single
 *                              survived attempt is weaker evidence than several).
 *   GUSDMAWEDGE OVERWRITETEST=1 REPEAT=3   the overwrite-in-place experiment
 *                              (see above), independent of DMATEST -- both
 *                              may be passed together in one run.
 *
 * Knobs shared with gustone.c's own convention (V=<n>, HZ=<n>, DUR=<ms>,
 * BASE=<hex>, IRQ=<n>, DMA=<n>, LATCH=0|1) -- see below; defaults match
 * GUSTONE's own proven-audible baseline (V=14 -> 44100Hz, avoids the
 * unrelated 28-voice dead-zone).
 *
 * Build (mirrors gustone.c -- pure DJGPP, no SDL headers):
 *   i586-pc-msdosdjgpp-gcc -O2 -Wall gusdmawedge.c -o GUSDMAWEDGE.EXE
 *
 * SAFETY NOTE ON THIS PROBE ITSELF: DMATEST=1 is the ONE deliberately risky
 * path in this whole probe suite. Every stage before it (bring-up, PIO
 * upload, voice start) is the same proven-safe sequence GUSTONE already
 * uses. Do not add any other GF1 port-READ anywhere else in DMATEST mode --
 * that would confound which specific operation caused a hang.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>     /* delay(), disable()/enable() */
#include <pc.h>      /* inportb/outportb */
#include <dpmi.h>    /* _go32_dpmi_seginfo, _go32_dpmi_allocate_dos_memory */
#include <go32.h>    /* dosmemput/dosmemget */

/* ---- GF1 I/O port map (verbatim from SDL_dosaudio_gus.c / gustone.c) ---- */
#define P2X0_MIXCTRL(b)   ((b) + 0x000)
#define P2XB_IRQDMA(b)    ((b) + 0x00B)
#define P3X2_VOICESEL(b)  ((b) + 0x102)
#define P3X3_REGSEL(b)    ((b) + 0x103)
#define P3X4_DATALO(b)    ((b) + 0x104)
#define P3X5_DATAHI(b)    ((b) + 0x105)
#define P3X7_DRAMIO(b)    ((b) + 0x107)

#define REG_DRAM_LO   0x43
#define REG_DRAM_HI   0x44
#define REG_RESET     0x4C
#define REG_VOICES    0x0E
#define REG_DMA_CTRL  0x41  /* GUS_REG_DMA_CTRL, verbatim from the SDL driver */
#define REG_DMA_ADDR  0x42  /* GUS_REG_DMA_ADDR */

#define VREG_CTRL      0x00
#define VREG_FREQ      0x01
#define VREG_STARTHI   0x02
#define VREG_STARTLO   0x03
#define VREG_ENDHI     0x04
#define VREG_ENDLO     0x05
#define VREG_RAMPRATE  0x06
#define VREG_RAMPSTART 0x07
#define VREG_RAMPEND   0x08
#define VREG_VOLUME    0x09
#define VREG_PAN       0x0C
#define VREG_VOLCTRL   0x0D

#define VC_STOP  0x03
#define VC_LOOP  0x08

static const unsigned char IRQ_CODES[16] = { 0,0,1,3,0,2,0,4, 0,0,0,5,6,0,0,7 };
static const unsigned char DMA_CODES[8]  = { 0,1,0,2,0,3,4,5 };

#define RATE_NUM 617400u

/* DMA chunk: <=64KB, 16-multiple (GF1 DMA dest reg 0x42 holds addr>>4),
   mirrors GUS_DMA_CHUNK in SDL_dosaudio_gus.c. Kept small here -- this probe
   only needs to prove the register sequence + poll loop run, not move a lot
   of data. */
#define DMA_CHUNK 4096u

/* ---- knobs --------------------------------------------------------------- */
static int k_base = 0x240, k_irq = 7, k_dma = 3, k_voices = 14;
static int k_hz = 440, k_dur_ms = 4000, k_latch = 1;
static int k_dmatest = 0, k_repeat = 1, k_overwritetest = 0;

/* OVERWRITETEST's own DRAM region -- clear of voice 0's tone (0x0) and
 * DMATEST's own region (0x10000). Never read by any voice in this probe. */
#define REGION_B_ADDR 0x20000ul

static FILE *g_log = NULL;

static void logln(const char *fmt, ...)
{
    va_list ap; char buf[256];
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); }
}

static void udelay(unsigned us) { unsigned i; for (i = 0; i < us; i++) (void)inportb(0x80); }

/* ---- GF1 register helpers (verbatim pattern from gustone.c) ------------- */
static void gus_w8(int base, unsigned char reg, unsigned char val)
{
    outportb(P3X3_REGSEL(base), reg);
    outportb(P3X5_DATAHI(base), val);
}
static void gus_w16(int base, unsigned char reg, unsigned short val)
{
    outportb(P3X3_REGSEL(base), reg);
    outportb(P3X4_DATALO(base), (unsigned char)(val & 0xFF));
    outportb(P3X5_DATAHI(base), (unsigned char)(val >> 8));
}
/* Bounded, single settle delay -- NOT a hot loop (the diag-wedge rule). Only
   ever called from SAFE contexts in this probe (bring-up / no active voice,
   or the one deliberate DMATEST exception, clearly logged). */
static unsigned char gus_r8(int base, unsigned char reg)
{
    outportb(P3X3_REGSEL(base), reg);
    udelay(2);
    return inportb(P3X5_DATAHI(base));
}
static void gus_select_voice(int base, int v) { outportb(P3X2_VOICESEL(base), (unsigned char)v); }
static void gus_set_dram_addr(int base, unsigned long addr)
{
    gus_w16(base, REG_DRAM_LO, (unsigned short)(addr & 0xFFFF));
    gus_w8(base, REG_DRAM_HI, (unsigned char)((addr >> 16) & 0x0F));
}
static void gus_poke(int base, unsigned long addr, unsigned char v)
{
    gus_set_dram_addr(base, addr);
    outportb(P3X7_DRAMIO(base), v);
}
static unsigned char gus_peek(int base, unsigned long addr)
{
    gus_set_dram_addr(base, addr);
    return inportb(P3X7_DRAMIO(base));
}

static void gus_reset_chip(int base)
{
    gus_w8(base, REG_RESET, 0x00); udelay(100);
    gus_w8(base, REG_RESET, 0x01); udelay(100);
}
static void gus_program_irq_dma(int base, int irq, int dma)
{
    unsigned char irqc = (irq >= 0 && irq < 16) ? IRQ_CODES[irq] : 0;
    unsigned char dmac = (dma >= 0 && dma < 8) ? DMA_CODES[dma] : 0;
    disable();
    outportb(P2X0_MIXCTRL(base), 0x00);
    outportb(P2XB_IRQDMA(base), irqc);
    outportb(P2X0_MIXCTRL(base), 0x40);
    outportb(P2XB_IRQDMA(base), dmac);
    enable();
}
static int gus_dram_roundtrip(int base)
{
    unsigned char a, b;
    disable();
    gus_poke(base, 0, 0xAA);
    gus_poke(base, 1, 0x55);
    a = gus_peek(base, 0);
    b = gus_peek(base, 1);
    enable();
    return (a == 0xAA && b == 0x55);
}

#define PIO_BURST 64u
static void gus_upload_pio(int base, unsigned long dram_addr, const unsigned char *src, unsigned long len)
{
    unsigned long i = 0;
    while (i < len) {
        unsigned long n = len - i, j;
        if (n > PIO_BURST) n = PIO_BURST;
        disable();
        for (j = 0; j < n; j++) {
            gus_set_dram_addr(base, dram_addr + i + j);
            outportb(P3X7_DRAMIO(base), src[i + j]);
        }
        enable();
        i += n;
    }
}

static void gus_silence_all_voices(int base, int nvoices)
{
    int v;
    disable();
    for (v = 0; v < nvoices; v++) {
        gus_select_voice(base, v);
        gus_w8(base, VREG_VOLCTRL, VC_STOP);
        gus_w8(base, VREG_CTRL, VC_STOP);
        gus_w16(base, VREG_STARTHI, 0);
        gus_w16(base, VREG_STARTLO, 0);
        gus_w16(base, VREG_ENDHI, 0);
        gus_w16(base, VREG_ENDLO, 0);
        gus_w16(base, VREG_VOLUME, 0);
        gus_w8(base, VREG_PAN, 0x07);
    }
    enable();
}

static unsigned short hz_to_fc(unsigned long playback_hz, unsigned long out_rate)
{
    unsigned long fc;
    if (!out_rate) return 0x400;
    fc = (unsigned long)(((unsigned long long)playback_hz << 10) / out_rate);
    if (fc > 0xFFFF) fc = 0xFFFF;
    return (unsigned short)fc;
}
static void encode_addr(unsigned long byteaddr, unsigned short *msw, unsigned short *lsw)
{
    /* 8-bit sample addressing (this probe only ever uploads 8-bit data). */
    *msw = (unsigned short)((byteaddr >> 7) & 0x1FFF);
    *lsw = (unsigned short)((byteaddr & 0x7F) << 9);
}

/* Start voice 0 LOOPING, driven through the GF1 ramp engine (gus-14, the
   proven-audible path -- see gustone.c's own VOL=ramp default and its
   comment for why a ramp-stopped direct 0x09 write is silent on real HW). */
static void start_loop_voice(int base, unsigned long start, unsigned long end,
                              unsigned long out_rate, unsigned long playback_hz)
{
    unsigned short smsw, slsw, emsw, elsw, cmsw, clsw, fc;

    fc = hz_to_fc(playback_hz, out_rate);

    disable();
    gus_select_voice(base, 0);
    gus_w16(base, VREG_FREQ, fc);
    enable();

    disable();
    gus_select_voice(base, 0);
    gus_w8(base, VREG_VOLCTRL, VC_STOP);
    gus_w8(base, VREG_RAMPSTART, 0x00);
    gus_w8(base, VREG_RAMPEND, 0xFF);   /* top of the 4.8 log range -> loud */
    gus_w8(base, VREG_RAMPRATE, 0x3F);  /* fastest -> reaches target in <1ms */
    gus_w16(base, VREG_VOLUME, 0x0000);
    gus_w8(base, VREG_VOLCTRL, 0x00);   /* RUN up */
    gus_w8(base, VREG_VOLCTRL, 0x00);   /* errata double-write */
    enable();

    disable();
    gus_select_voice(base, 0);
    gus_w8(base, VREG_PAN, 0x08); /* center */
    enable();

    encode_addr(start, &smsw, &slsw);
    encode_addr(end, &emsw, &elsw);
    encode_addr(start, &cmsw, &clsw);

    disable();
    gus_select_voice(base, 0);
    gus_w8(base, VREG_CTRL, VC_STOP);
    gus_w16(base, VREG_STARTHI, smsw);
    gus_w16(base, VREG_STARTLO, slsw);
    gus_w16(base, VREG_ENDHI, emsw);
    gus_w16(base, VREG_ENDLO, elsw);
    gus_w8(base, VREG_CTRL, VC_LOOP);
    gus_w8(base, VREG_CTRL, VC_LOOP); /* errata double-write */
    enable();
}
static void stop_voice(int base, int voice)
{
    disable();
    gus_select_voice(base, voice);
    gus_w8(base, VREG_VOLCTRL, VC_STOP);
    gus_w8(base, VREG_CTRL, VC_STOP);
    enable();
}

/* ---- the risky operation: DMA upload while voice 0 keeps playing -------- */

/* Mirrors gus_upload_dma() in SDL_dosaudio_gus.c: mask/program/unmask the
   host 8237 (8-bit channel), program the GF1 DMA dest + enable, then poll
   the HOST 8237 status FIRST, falling through to a GF1 port-READ
   (gus_r8(REG_DMA_CTRL)) if the 8237 hasn't signalled yet -- byte-for-byte
   the same completion-wait shape as the real driver, so this probe answers
   the real driver's own risk, not a different one. Returns 1 if the poll
   loop returned via EITHER path within its bound, 0 if it timed out
   (0 does NOT necessarily mean "wedged" -- the machine could still be alive
   and just never see completion signalled; a true wedge means this function
   never returns at all and no further log line appears). */
static int gus_dma_upload_once(int base, int dma, unsigned long dram_addr,
                                const unsigned char *src, unsigned long len)
{
    _go32_dpmi_seginfo bounce_seg;
    unsigned long physical, off;
    unsigned char physical_page;
    static const int page_ports[4] = { 0x87, 0x83, 0x81, 0x82 };
    int all_ok = 1;

    memset(&bounce_seg, 0, sizeof(bounce_seg));
    /* 64KB-page-safety doubling, mirrors DOS_AllocateDMAMemory exactly. */
    bounce_seg.size = (int)((DMA_CHUNK * 2 + 15) / 16);
    if (_go32_dpmi_allocate_dos_memory(&bounce_seg) != 0) {
        logln("gusdmawedge:   DMA bounce-buffer alloc FAILED (DOS conventional "
              "memory exhausted?) -- aborting this DMA attempt, voice 0 left "
              "untouched.");
        return 0;
    }
    physical = (unsigned long)bounce_seg.rm_segment * 16UL;
    if ((physical >> 16) != ((physical + DMA_CHUNK) >> 16)) {
        physical += DMA_CHUNK; /* use the second half if the first straddles */
    }
    physical_page = (unsigned char)((physical >> 16) & 0xFF);

    for (off = 0; off < len; off += DMA_CHUNK) {
        unsigned long nbytes = len - off;
        unsigned long dma_bytes;
        unsigned long dest = dram_addr + off;
        int spin, done = 0;
        if (nbytes > DMA_CHUNK) nbytes = DMA_CHUNK;
        dma_bytes = nbytes - 1;

        /* stage the chunk into the bounce buffer via real-mode DOS memory. */
        dosmemput(src + off, nbytes, physical);

        disable();
        (void)inportb(0x08); /* clear stale 8237 terminal-count before arming */
        outportb(0x0A, 0x04 | dma);                              /* mask channel */
        outportb(0x0B, 0x48 | dma);                              /* single/read/one-shot */
        outportb(page_ports[dma], physical_page);
        outportb(0x0C, 0x00);                                    /* clear flip-flop */
        outportb((unsigned)(dma * 2), (unsigned char)(physical & 0xFF));
        outportb((unsigned)(dma * 2), (unsigned char)((physical >> 8) & 0xFF));
        outportb(0x0C, 0x00);
        outportb((unsigned)(dma * 2 + 1), (unsigned char)(dma_bytes & 0xFF));
        outportb((unsigned)(dma * 2 + 1), (unsigned char)((dma_bytes >> 8) & 0xFF));
        outportb(0x0A, (unsigned)dma);                           /* unmask */

        gus_w16(base, REG_DMA_ADDR, (unsigned short)((dest >> 4) & 0xFFFF));
        gus_w8(base, REG_DMA_CTRL, 0x01);
        enable();

        /* THE RISKY POLL: 8237 status first (not a GF1 port), then -- if not
           yet done -- a GF1 port-READ (gus_r8) while voice 0 remains active.
           Bounded (100000 iters, matches the real driver exactly) so even a
           non-wedging failure terminates instead of spinning forever. */
        for (spin = 0; spin < 100000; spin++) {
            if (inportb(0x08) & (1 << dma)) { done = 1; break; }
            if (gus_r8(base, REG_DMA_CTRL) & 0x40) { done = 1; break; }
        }
        gus_w8(base, REG_DMA_CTRL, 0x00);
        if (!done) all_ok = 0;
    }

    _go32_dpmi_free_dos_memory(&bounce_seg);
    return all_ok;
}

/* ---- arg parsing (same convention as gustone.c) -------------------------- */
static int hexarg(const char *s) { return (int)strtol(s, NULL, 16); }
static int decarg(const char *s) { return (int)strtol(s, NULL, 10); }

static void parse_args(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        char key[16]; const char *eq = strchr(argv[i], '='); int kl, j;
        if (!eq) continue;
        kl = (int)(eq - argv[i]); if (kl > 15) kl = 15;
        memcpy(key, argv[i], kl); key[kl] = 0;
        for (j = 0; key[j]; j++) key[j] = (char)toupper((unsigned char)key[j]);
        eq++;
        if      (!strcmp(key, "BASE"))    k_base = hexarg(eq);
        else if (!strcmp(key, "IRQ"))     k_irq = decarg(eq);
        else if (!strcmp(key, "DMA"))     k_dma = decarg(eq);
        else if (!strcmp(key, "V"))       k_voices = decarg(eq);
        else if (!strcmp(key, "HZ"))      k_hz = decarg(eq);
        else if (!strcmp(key, "DUR"))     k_dur_ms = decarg(eq);
        else if (!strcmp(key, "LATCH"))   k_latch = decarg(eq);
        else if (!strcmp(key, "DMATEST")) k_dmatest = decarg(eq);
        else if (!strcmp(key, "REPEAT"))  k_repeat = decarg(eq);
        else if (!strcmp(key, "OVERWRITETEST")) k_overwritetest = decarg(eq);
    }
}

#define TONE_LEN 2048
static unsigned char g_tone[TONE_LEN];
static unsigned char g_dma_payload[DMA_CHUNK];

static unsigned char g_region_b_payload[DMA_CHUNK];

int main(int argc, char **argv)
{
    int present, period, i, r;
    unsigned long out_rate, end_byte;
    unsigned long dma_dest;
    int any_test = 0;

    g_log = fopen("GUSDMAWEDGE.LOG", "w");
    parse_args(argc, argv);
    if (k_voices < 1) k_voices = 1;
    if (k_voices > 32) k_voices = 32;
    out_rate = RATE_NUM / (unsigned long)k_voices;
    any_test = k_dmatest || k_overwritetest;

    logln("==== GUSDMAWEDGE -- GF1 DMA-upload-vs-active-voice wedge probe ====");
    logln("gusdmawedge: mode=%s%s%s base=0x%X irq=%d dma=%d V=%d out_rate=%luHz "
          "hz=%d dur=%dms repeat=%d",
          any_test ? "" : "SAFE", k_dmatest ? "DMATEST" : "",
          k_overwritetest ? (k_dmatest ? "+OVERWRITETEST" : "OVERWRITETEST") : "",
          k_base, k_irq, k_dma, k_voices, out_rate, k_hz, k_dur_ms, k_repeat);
    if (k_dmatest)
        logln("gusdmawedge: *** DMATEST=1 -- this WILL attempt a DMA upload while "
              "voice 0 is actively looping. If this machine is real hardware and "
              "the hypothesis is confirmed, IT MAY HANG HERE. That is the point "
              "of this probe -- do not run DMATEST=1 on real hardware without the "
              "operator's own explicit go-ahead for a hang risk. ***");
    if (k_overwritetest)
        logln("gusdmawedge: *** OVERWRITETEST=1 -- this WILL overwrite a DRAM "
              "region already written once earlier in this run, while voice 0 "
              "is actively looping (a DIFFERENT region). Same hang risk as "
              "DMATEST -- do not run on real hardware without the operator's "
              "own explicit go-ahead. ***");
    if (!any_test)
        logln("gusdmawedge: SAFE mode -- exercises bring-up/upload/voice-start "
              "only, same proven-safe sequence GUSTONE already uses. No risky "
              "attempt will be made. Pass DMATEST=1 and/or OVERWRITETEST=1 for "
              "the actual experiments.");

    /* --- bring-up (identical shape to gustone.c) -------------------------- */
    logln("gusdmawedge: [bring-up] reset chip...");
    gus_reset_chip(k_base);
    present = gus_dram_roundtrip(k_base);
    logln("gusdmawedge: [bring-up] DRAM roundtrip present=%d", present);
    if (!present) {
        logln("gusdmawedge: RESULT=NO_CARD at base 0x%X. Check ULTRASND port + "
              "pgusinit /mode gus.", k_base);
        if (g_log) fclose(g_log);
        return 2;
    }
    if (k_latch) {
        logln("gusdmawedge: [bring-up] program IRQ/DMA latches (irq=%d dma=%d)...",
              k_irq, k_dma);
        gus_program_irq_dma(k_base, k_irq, k_dma);
    }
    gus_w8(k_base, REG_VOICES, (unsigned char)(0xC0 | (k_voices - 1)));
    gus_w8(k_base, REG_RESET, 0x07);
    gus_w8(k_base, REG_RESET, 0x07);
    udelay(100);
    gus_silence_all_voices(k_base, 32);
    outportb(P2X0_MIXCTRL(k_base), 0x08); /* line-out enabled, written last */

    /* --- upload + start the LOOPING voice-0 tone (safe: upload-before-start) */
    period = (int)(out_rate / (unsigned long)(k_hz > 0 ? k_hz : 440));
    if (period < 2) period = 2;
    if (period > TONE_LEN) period = TONE_LEN;
    {
        int nper = TONE_LEN / period, nsamp = nper * period;
        for (i = 0; i < nsamp; i++)
            g_tone[i] = ((i % period) < (period / 2)) ? 0xFF : 0x00;
        logln("gusdmawedge: [upload] voice-0 tone %d bytes -> DRAM 0x0 (PIO, "
              "safe -- no voice active yet)", nsamp);
        gus_upload_pio(k_base, 0, g_tone, (unsigned long)nsamp);
        end_byte = (unsigned long)(nsamp - 1);
    }

    /* OVERWRITETEST's own "written once, safely, before any voice existed"
     * baseline -- the SAME safe window (no voice active yet) every other
     * upload in this probe already uses. This establishes region B as an
     * already-used address, exactly like a gus_pcm_stream half-buffer's own
     * initial fill at stream-open time, BEFORE the later overwrite-while-
     * voice-0-plays test below. */
    if (k_overwritetest) {
        for (i = 0; i < DMA_CHUNK; i++)
            g_region_b_payload[i] = (unsigned char)(0x5A ^ (i & 0xFF)); /* initial fill pattern */
        logln("gusdmawedge: [upload] region B initial fill, %u bytes -> DRAM "
              "0x%lX (PIO, safe -- no voice active yet; this is the write "
              "OVERWRITETEST will later overwrite while voice 0 plays)",
              DMA_CHUNK, REGION_B_ADDR);
        gus_upload_pio(k_base, REGION_B_ADDR, g_region_b_payload, (unsigned long)DMA_CHUNK);
    }

    logln("gusdmawedge: [voice0] START looping tone @ %luHz (V=%d) -- will play "
          "for %dms total (plus any risky attempts inside that window)...",
          out_rate, k_voices, k_dur_ms);
    start_loop_voice(k_base, 0, end_byte, out_rate, out_rate);
    logln("gusdmawedge: [voice0] voice 0 reports STARTED (this line proves the "
          "machine survived starting the loop -- unremarkable, but establishes "
          "the baseline before anything risky happens).");

    if (!any_test) {
        delay(k_dur_ms);
        stop_voice(k_base, 0);
        logln("gusdmawedge: RESULT=SAFE_DONE -- voice-0 loop played + stopped "
              "cleanly, no risky attempt made (pass DMATEST=1 and/or "
              "OVERWRITETEST=1 for the real tests).");
        gus_w8(k_base, REG_RESET, 0x00);
        if (g_log) fclose(g_log);
        return 0;
    }

    /* --- experiment 1: DMA upload to a FRESH region while voice 0 plays --- */
    if (k_dmatest) {
        dma_dest = 0x10000; /* well clear of the voice-0 tone at DRAM 0x0 */
        for (i = 0; i < DMA_CHUNK; i++)
            g_dma_payload[i] = (unsigned char)(0xA5 ^ (i & 0xFF)); /* a recognizable pattern */

        for (r = 0; r < k_repeat; r++) {
            logln("gusdmawedge: ---- DMA attempt %d/%d ----", r + 1, k_repeat);
            logln("gusdmawedge: ABOUT TO ATTEMPT DMA UPLOAD WHILE VOICE 0 IS ACTIVE "
                  "(dest=0x%lX chan=%d chunk=%u bytes) -- if this machine hangs, "
                  "THIS is the last line you will see.",
                  dma_dest, k_dma, DMA_CHUNK);
            {
                int ok = gus_dma_upload_once(k_base, k_dma, dma_dest, g_dma_payload,
                                              (unsigned long)DMA_CHUNK);
                logln("gusdmawedge: DMA upload attempt %d/%d RETURNED, poll_ok=%d "
                      "(the machine did NOT hang on this attempt -- refutes the "
                      "immediate-hang hypothesis for this one attempt; poll_ok=0 "
                      "means the completion poll timed out, a DIFFERENT, milder "
                      "failure than a hang).", r + 1, k_repeat, ok);
            }
            delay(200);
        }

        /* Bounded, single-pass, no-active-voice verify happens further down
         * (after voice 0 is stopped) so it never itself becomes a risky
         * concurrent-read -- see the shared verify block below. */
    }

    /* --- experiment 2: OVERWRITE an address already written once, while --- */
    /* --- voice 0 (a DIFFERENT region) keeps playing ----------------------- */
    if (k_overwritetest) {
        for (i = 0; i < DMA_CHUNK; i++)
            g_region_b_payload[i] = (unsigned char)(0x3C ^ (i & 0xFF)); /* NEW pattern, distinct from the initial fill */

        for (r = 0; r < k_repeat; r++) {
            logln("gusdmawedge: ---- OVERWRITE attempt %d/%d ----", r + 1, k_repeat);
            logln("gusdmawedge: ABOUT TO OVERWRITE REGION B (already written once "
                  "pre-voice) WHILE VOICE 0 IS ACTIVE (dest=0x%lX chan=%d "
                  "chunk=%u bytes) -- if this machine hangs, THIS is the last "
                  "line you will see.",
                  REGION_B_ADDR, k_dma, DMA_CHUNK);
            {
                int ok = gus_dma_upload_once(k_base, k_dma, REGION_B_ADDR,
                                              g_region_b_payload, (unsigned long)DMA_CHUNK);
                logln("gusdmawedge: OVERWRITE attempt %d/%d RETURNED, poll_ok=%d "
                      "(the machine did NOT hang on this attempt -- refutes the "
                      "immediate-hang hypothesis for overwriting an already-used "
                      "address while a different voice plays; poll_ok=0 means "
                      "the completion poll timed out, a DIFFERENT, milder "
                      "failure than a hang).", r + 1, k_repeat, ok);
            }
            delay(200);
        }
    }

    stop_voice(k_base, 0);
    logln("gusdmawedge: [voice0] stopped (safe -- no longer active for the "
          "verify step below).");

    /* Bounded, single-pass, no-active-voice verify -- mirrors GUSDUMP's own
       "safe because nothing is playing" framing. Tells you whether a
       *survived* attempt actually moved correct data, independent of the
       wedge question. */
    if (k_dmatest) {
        int mismatches = 0;
        unsigned long k;
        disable();
        for (k = 0; k < 64; k++) { /* bounded sample, not the whole chunk */
            unsigned char got = gus_peek(k_base, dma_dest + k);
            unsigned char want = (unsigned char)(0xA5 ^ (k & 0xFF));
            if (got != want) mismatches++;
        }
        enable();
        logln("gusdmawedge: [verify] post-hoc DRAM check @0x%lX (DMATEST region), "
              "first 64 bytes: %d/64 mismatches (0 = DMA transferred this region "
              "correctly).", dma_dest, mismatches);
    }
    if (k_overwritetest) {
        int mismatches = 0;
        unsigned long k;
        disable();
        for (k = 0; k < 64; k++) { /* bounded sample, not the whole chunk */
            unsigned char got = gus_peek(k_base, REGION_B_ADDR + k);
            unsigned char want = (unsigned char)(0x3C ^ (k & 0xFF)); /* the LAST overwrite's pattern, not the initial fill */
            if (got != want) mismatches++;
        }
        enable();
        logln("gusdmawedge: [verify] post-hoc DRAM check @0x%lX (region B), "
              "first 64 bytes: %d/64 mismatches (0 = the LAST overwrite landed "
              "correctly -- a mismatch here that instead matches the INITIAL "
              "0x5A fill pattern would mean the overwrite never actually took, "
              "surviving only because it silently no-op'd, not because "
              "overwriting a live address is genuinely safe).",
              REGION_B_ADDR, mismatches);
    }

    logln("gusdmawedge: RESULT=%s%s%s_DONE -- if you are reading this, the "
          "machine survived every risky attempt this run. Re-run a few times "
          "/ vary REPEAT and chunk exposure before trusting this generally, "
          "per this campaign's own 'one clean pass is evidence, not proof' "
          "discipline.",
          k_dmatest ? "DMATEST" : "", (k_dmatest && k_overwritetest) ? "_" : "",
          k_overwritetest ? "OVERWRITETEST" : "");
    gus_w8(k_base, REG_RESET, 0x00);
    if (g_log) fclose(g_log);
    return 0;
}
