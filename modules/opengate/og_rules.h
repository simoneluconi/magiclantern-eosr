/** \file
 * Open gate 4K: rules that derive Canon's develop-record fields from the geometry.
 * Pure functions, no camera access: included by opengate.c and by the host test
 * (ML-R6/research/opengate/audit/opengate/rules_test.c), which checks them against the
 * stock 4K30 and 4K60 records of the ROM dump. Sources: FRESH-REVIEW.md 3 and 7.2,
 * AUDIT.md R3/R4.
 */

#ifndef _og_rules_h_
#define _og_rules_h_

#define OG_VF_MAX 48

/* every vertical field of the develop record from the input bands (pipe 0 offsets) */
static int og_vfields(uint32_t b0, uint32_t b1, uint16_t * off, uint32_t * val)
{
    int n = 0;
    #define VF(o, v) do { off[n] = (o); val[n] = (v); n++; } while (0)
    VF(0x1DC, b0 + 23); VF(0x1E4, b1 + 23);          /* s0 V */
    VF(0x204, b0 + 23); VF(0x20C, b1 + 23);          /* s1 V */
    VF(0x234, b0 + 22); VF(0x244, b1 + 24);          /* s1 window ends */
    VF(0x254, b0 + 21); VF(0x25C, b1 + 21);          /* s3 V */
    VF(0x274, b0 + 21); VF(0x27C, b1 + 21);          /* s5 V */
    VF(0x2A4, b0 + 21); VF(0x2B4, b1 + 21);          /* s5 window ends */
    uint32_t r0 = (b0 + 30 + 7) / 8, r1 = (b1 + 30 + 7) / 8;
    VF(0x33C, 8 * r0 + 7); VF(0x344, 8 * r1 + 7);    /* s11: pyramid input */
    VF(0x36C, 14 + b0 + 21); VF(0x37C, 30 + b1 + 21);/* s11 window ends */
    static const uint16_t lv1_0[5] = { 0x2C4, 0x2FC, 0x31C, 0x398, 0x3C8 };
    static const uint16_t lv1_1[5] = { 0x2CC, 0x304, 0x324, 0x3A0, 0x3D8 };
    for (int i = 0; i < 5; i++) { VF(lv1_0[i], r0); VF(lv1_1[i], r1); }   /* 1/8 level */
    uint32_t l2_0 = r0 + 4 + (1 - (r0 + 4) % 2), l2_1 = r1 + 4 + (1 - (r1 + 4) % 2);
    VF(0x428, l2_0); VF(0x430, l2_1);                /* s17 V */
    VF(0x458, r0 + 4); VF(0x468, r1 + 8);            /* s17 window ends */
    uint32_t s0 = (l2_0 - 1) / 2, s1 = (l2_1 - 1) / 2;
    static const uint16_t l2s_0[4] = { 0x3E8, 0x408, 0x484, 0x4B4 };
    static const uint16_t l2s_1[4] = { 0x3F0, 0x410, 0x48C, 0x4C4 };
    for (int i = 0; i < 4; i++) { VF(l2s_0[i], s0); VF(l2s_1[i], s1); }   /* s13, s15, s18 */
    VF(0x4D4, s1 / 8); VF(0x4DC, s1 / 8);            /* s19 V */
    VF(0x524, s0 + 8); VF(0x534, s1 + 8);            /* s21 window ends */
    #undef VF
    return n;
}

/* pyramid EDMAC descriptors: rows per band and band jumps (pipe 0 offsets) */
static int og_vdma(uint32_t b0, uint32_t b1, uint16_t * off, uint32_t * val)
{
    int n = 0;
    uint32_t r0 = (b0 + 30 + 7) / 8, r1 = (b1 + 30 + 7) / 8;
    uint32_t l2_0 = r0 + 4 + (1 - (r0 + 4) % 2), l2_1 = r1 + 4 + (1 - (r1 + 4) % 2);
    uint32_t s0 = (l2_0 - 1) / 2, s1 = (l2_1 - 1) / 2;
    #define VD(o, v) do { off[n] = (o); val[n] = (uint32_t) (v); n++; } while (0)
    VD(0x90, r0); VD(0x98, r1);
    VD(0x74, -(int32_t) (1370 * r0 + 22)); VD(0x7C, -(int32_t) (1370 * r1 + 22));
    VD(0xCC, s0); VD(0xD4, s1);
    VD(0xB0, -(int32_t) (686 * s0 + 20)); VD(0xB8, -(int32_t) (686 * s1 + 20));
    #undef VD
    return n;
}

/* scale block of one pipe (u16 output bands, start, last-band phase, step, 2^27/Vout) */
struct og_scale { uint16_t n1, n2; uint32_t start_i, start_r, phase_i, phase_r, step_i, step_r, recip; };

static struct og_scale og_scale_rule(uint32_t vin, uint32_t vout, uint32_t b0)
{
    struct og_scale s;
    uint32_t n1 = b0;
    while (n1 > 0 && ((2 * n1 + 1) * vin >= (2 * b0 + 3) * vout || (n1 & 1))) n1--;
    uint32_t two_out = 2 * vout, P = 2 * vin * (1 + 2 * n1);
    s.n1 = n1; s.n2 = vout - n1;
    s.start_i = (2 * vin) / two_out; s.start_r = (2 * vin) % two_out;
    s.phase_i = P / two_out - 2 * (b0 - 3); s.phase_r = P % two_out;
    s.step_i = (4 * vin) / two_out; s.step_r = (4 * vin) % two_out;
    s.recip = ((1u << 27) + vout / 2) / vout;
    return s;
}

static int og_scale_matches(const uint8_t * pipe, const struct og_scale * s)
{
    return *(uint16_t *) (pipe + 0x8EE) == s->n1 && *(uint16_t *) (pipe + 0x8F0) == s->n2
        && *(uint32_t *) (pipe + 0x918) == s->start_i && *(uint32_t *) (pipe + 0x91C) == s->start_r
        && *(uint32_t *) (pipe + 0x924) == s->phase_i && *(uint32_t *) (pipe + 0x928) == s->phase_r
        && *(uint32_t *) (pipe + 0x93C) == s->step_i && *(uint32_t *) (pipe + 0x940) == s->step_r
        && *(uint32_t *) (pipe + 0x944) == s->recip;
}

static void og_scale_write(uint8_t * pipe, const struct og_scale * s)
{
    *(uint16_t *) (pipe + 0x8EE) = s->n1;
    *(uint16_t *) (pipe + 0x8F0) = s->n2;
    *(uint32_t *) (pipe + 0x918) = s->start_i;
    *(uint32_t *) (pipe + 0x91C) = s->start_r;
    *(uint32_t *) (pipe + 0x924) = s->phase_i;
    *(uint32_t *) (pipe + 0x928) = s->phase_r;
    *(uint32_t *) (pipe + 0x93C) = s->step_i;
    *(uint32_t *) (pipe + 0x940) = s->step_r;
    *(uint32_t *) (pipe + 0x944) = s->recip;
}

static uint32_t og_gcd(uint32_t a, uint32_t b)
{
    while (b) { uint32_t t = a % b; a = b; b = t; }
    return a;
}

#endif
