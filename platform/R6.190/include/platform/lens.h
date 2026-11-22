#ifndef _platform_lens_h
#define _platform_lens_h

/* Digic 8 brings new properties:
 * PROP_LENS_STATIC_DATA  = PROP_LENS + PROP_LENS_NAME + ???
 * PROP_LENS_DYNAMIC_DATA = PROP_LV_LENS + ???
 *
 * Those are quite huge, they size depend on camera.
 * So far we have data from following models:
 *             M50   SX740    R     RP    SX70   250D   850D    R5   R6 1.9.0
 * STATIC     0x138  0x178  0x184  0x184  0x180  0x180  0x1C8  0x1C8  0x1D0
 * DYNAMIC    0x84   0x8C   0x90   0x90   0x90   0x8C   0x90   0x94   0x8C
 * DryOS ICU   P2     P3     P4     P4     P4     P5     P8     P9
 *
 * A lot of PROP_LENS_STATIC_DATA can be decoded via `readid` evshell function.
 * Some can be decoded via ShootingInfoEx (models like SX70 don't have readid...)
 *
 * R6 1.9.0 layouts (ROM 1.9.0):
 *  - sizes: PROP_LENS_DYNAMIC_DATA is registered with 0x8C bytes (0xE058A96A),
 *    PROP_LENS_STATIC_DATA with 0x1D0 (0xE058A8EC); Canon also prints them
 *    ("LensDynamicInfo Size:%x LensStaticInfo Size:%x", 0xE0316354).
 *  - dynamic field names: Canon's print of the structure (0xE0585B3E,
 *    "AVEF:0x%x", "[FL]%d [NearAbs(cm)]%d [NearInf(cm)]%d [HRFL0.1]%d"),
 *    ShootingInfoEx (0xE031550E), LensIf_GetFlAndStatusUnCache (0xE0749442),
 *    LensIf_GetISSwitch (0xE0749ADC, st3).
 *  - static field offsets: Canon's `readid` print (0xE0584718..0xE0584E06);
 *    same as the R5 layout up to 0xF0, then shifted by 2 or 4 bytes.
 * Checked against property dumps from the camera (andrewpoison's R6.190 port
 * logs, Sigma 35mm F1.4 Art and a 150-600mm zoom). Natural alignment, no
 * packing; the checks at the end make the compiler verify the offsets ML reads.
 */
struct prop_lens_static_data
{
        uint8_t                 attached;                      // 0x000
        uint8_t                 type;                          // 0x001: 90 EF, 91 RF
        uint8_t                 LENSID[38];                    // 0x002 LENSID
        uint16_t                lens_id;                       // 0x028: and yes, this one is lens_id...
        uint16_t                lens_id_ext;                   // 0x02A
        uint16_t                fl_wide;                       // 0x02C
        uint16_t                fl_tele;                       // 0x02E
        uint8_t                 lens_serial[5];                // 0x030
        uint8_t                _unk_02[24];                    // 0x035: not referenced in readid
        uint8_t                 extender_id[6];                // 0x04D
        uint8_t                 lens_firm_ver[3];              // 0x053
        uint8_t                 field_vision;                  // 0x056
        uint8_t                 lens_type;                     // 0x057
        uint8_t                 extenderMountInfo;             // 0x058
        uint8_t                 lens_name_len;                 // 0x059
        char                    lens_name[73];                 // 0x05A
        uint8_t                _unk_03[65];                    // 0x0A3: not referenced in readid
        uint8_t                 mount_size;                    // 0x0E4
        uint8_t                 lens_switch_exists;            // 0x0E5
        uint8_t                 lens_af_func_exists;           // 0x0E6
        uint8_t                 lens_mf_funct_exists;          // 0x0E7
        uint8_t                 lens_is_switch_exists;         // 0x0E8
        uint8_t                 lens_is_funct_exists;          // 0x0E9
        uint8_t                 af_speed_setting_available;    // 0x0EA
        uint8_t                 dafLimitFno;                   // 0x0EB
        uint8_t                 distortionCorrectionInfo;      // 0x0EC
        uint8_t                 bcfInfo;                       // 0x0ED
        uint8_t                 lens_id_1292;                  // 0x0EE
        uint8_t                 emd_hot_limit;                 // 0x0EF
        uint8_t                 aberationControl;              // 0x0F0
        uint8_t                 popeye_info;                   // 0x0F1: new on R6 1.9.0
        uint8_t                 mlSupportCinemaLens;           // 0x0F2: new on R6 1.9.0
        uint8_t                _pad_01;                        // 0x0F3
        uint16_t                zoom_pos_size;                 // 0x0F4 (R5: 0x0F2)
        uint16_t                focus_pos_size;                // 0x0F6
        uint16_t                fine_focus_size;               // 0x0F8
        uint16_t                av_stop_div;                   // 0x0FA
        uint8_t                 av_slow_enable;                // 0x0FC
        uint8_t                 av_dlp_lens;                   // 0x0FD
        uint16_t                av_dlp_max_spd;                // 0x0FE
        uint16_t                av_dlp_silent_spd;             // 0x100
        uint16_t                av_dlp_min_spd;                // 0x102
        uint8_t                _unk_04[150];                   // 0x104
        uint8_t                 extendMagnificationVal;        // 0x19A
        uint8_t                _unk_05;                        // 0x19B
        uint16_t                ois_shift_max;                 // 0x19C
        uint8_t                 colorBalance;                  // 0x19E
        uint8_t                 pza_exists;                    // 0x19F
        uint8_t                 pza_id[5];                     // 0x1A0
        uint8_t                 pza_firm_ver[3];               // 0x1A5
        uint8_t                 pza_firmup;                    // 0x1A8
        uint8_t                 dlAdp_count;                   // 0x1A9 (adapter check 0xE0749B3C)
        uint8_t                _pad_02[2];                     // 0x1AA
        uint32_t                dlAdp1_id;                     // 0x1AC
        uint8_t                 dlAdp1_func1;                  // 0x1B0
        uint8_t                 dlAdp1_firm_ver[3];            // 0x1B1
        uint32_t                dlAdp2_id;                     // 0x1B4
        uint8_t                 dlAdp2_func1;                  // 0x1B8
        uint8_t                 dlAdp2_firm_ver[3];            // 0x1B9
        uint8_t                _unk_06;                        // 0x1BC
        uint8_t                 demandWarnDispFromLens;        // 0x1BD
        uint8_t                 demandWarnDispFromAdp;         // 0x1BE
        uint8_t                _unk_07;                        // 0x1BF
        uint16_t                high_res_focal_length_tele;    // 0x1C0: new on R6 1.9.0, in 0.1 mm
        uint16_t                high_res_focal_length_wide;    // 0x1C2: new on R6 1.9.0, in 0.1 mm
        uint8_t                _unk_08[12];                    // 0x1C4
};

SIZE_CHECK_STRUCT( prop_lens_static_data, 0x1D0 );

/*
 * PROP_LENS_DYNAMIC_DATA structure, R6 1.9.0
 *
 * Canon's own print of this structure names most fields:
 * _prints_dynamic_data(dynamic*, static*) = 0xE0585B3E ("AVEF:0x%x"...).
 * ShootingInfoEx (0xE031550E) embeds a copy
 * ("ShootingInfoEx:LensDynamicInfo avef:%x"...).
 *
 * Apertures are signed APEX * 768 ("Code768", see CONFIG_EXPO_CODE768);
 * 0xDF00 when there is no value.
 */
struct prop_lens_dynamic_data {
        int16_t                 AVEF;             // 0x00
        int16_t                 AVO;              // 0x02: ShootingInfoEx avo, widest aperture
        int16_t                 AVMAX;            // 0x04: ShootingInfoEx avmax, smallest aperture
        int16_t                 AVD;              // 0x06
        int16_t                 NowAvRF;          // 0x08
        int16_t                 NowAvEF;          // 0x0A
        uint8_t                _unk_01[4];        // 0x0C
        int16_t                 AVEF_image;       // 0x10
        int16_t                 AVO_image;        // 0x12
        uint16_t                jsstep;           // 0x14
        uint8_t                _unk_02[4];        // 0x16
        uint16_t                IDC;              // 0x1A
        uint8_t                _unk_03[2];        // 0x1C
        uint16_t                po;               // 0x1E
        uint16_t                po05;             // 0x20
        uint16_t                po10;             // 0x22
        uint16_t                po00;             // 0x24
        uint16_t                po15;             // 0x26
        uint16_t                Npo;              // 0x28
        uint16_t                po0;              // 0x2A
        uint16_t                po25;             // 0x2C
        uint16_t                po50;             // 0x2E
        uint16_t                po75;             // 0x30
        uint16_t                po100;            // 0x32
        uint16_t                po125;            // 0x34
        uint16_t                po150;            // 0x36
        uint16_t                po175;            // 0x38
        uint16_t                po200;            // 0x3A
        uint8_t                 v0;               // 0x3C: vignetting
        uint8_t                 v1;
        uint8_t                 v2;
        uint8_t                 v3;
        uint16_t                FL;               // 0x40: focal length in mm, "[FL]"
        uint16_t                focal_len_image;  // 0x42: ShootingInfoEx focal_length_image
        uint16_t                focus_far;        // 0x44: cm, "[NearInf(cm)]", ShootingInfoEx abs_inf
        uint16_t                focus_near;       // 0x46: cm, "[NearAbs(cm)]", ShootingInfoEx abs_near
        uint16_t                zoomPos;          // 0x48: ShootingInfoEx zoom_pos
        uint16_t                focusPos;         // 0x4A
        uint16_t                fineFocusPos;     // 0x4C: ShootingInfoEx fine_focus_pos
        uint16_t                HighResoZoomPos;  // 0x4E: ShootingInfoEx high_res_zoom_pos
        uint16_t                HighResoFocusPos; // 0x50: ShootingInfoEx high_res_focus_pos
        uint8_t                _unk_04[8];        // 0x52
        int8_t                  abstat;           // 0x5A: lens aberration related
        uint8_t                 st1;              // 0x5B
        uint8_t                 st2;              // 0x5C: & 0x80 true -> MF, false -> AF (assumed; also set without a lens)
        uint8_t                 st3;              // 0x5D: & 0x0F looks equiv. to PROP_LV_LENS_STABILIZE
        uint8_t                 st4;              // 0x5E
        uint8_t                 st5;              // 0x5F
        uint8_t                _unk_05[3];        // 0x60
        uint8_t                 truePfOn;         // 0x63
        uint8_t                 FcsSt1;           // 0x64: FocusStep?
        uint8_t                 FcsSt2;
        uint8_t                 FcsSt3;
        uint8_t                 FcsSt4;
        uint8_t                 ZmSt1;            // 0x68: ZoomStep?
        uint8_t                 ZmSt2;
        uint8_t                 ZmSt3;
        uint8_t                 ZmSt4;
        uint8_t                _unk_06[4];        // 0x6C
        int16_t                 ts_shift;         // 0x70: via ShootingInfoEx
        int8_t                  ts_tilt;          // 0x72: via ShootingInfoEx
        int8_t                  ts_all_revo;      // 0x73: via ShootingInfoEx
        int8_t                  ts_ts_revo;       // 0x74: via ShootingInfoEx
        uint8_t                _unk_07[5];        // 0x75
        uint16_t                HRFL;             // 0x7A: focal length in 0.1 mm, "[HRFL0.1]"
        int8_t                  temperature;      // 0x7C: -128 = no value
        uint8_t                _unk_08[3];        // 0x7D
        uint32_t                LENSEr;           // 0x80
        uint32_t               _unk_09;           // 0x84
        uint32_t               _unk_0a;           // 0x88
};

SIZE_CHECK_STRUCT( prop_lens_dynamic_data, 0x8C );

/* offsets of the fields src/lens.c reads, and a few anchors of the shifted part */
#define LENS_OFFSET_CHECK(s, f, o) \
        static uint8_t __attribute__((unused)) \
        __offset_check_##s##_##f[__builtin_offsetof(struct s, f) == (o) ? 0 : -1]

LENS_OFFSET_CHECK( prop_lens_dynamic_data, AVO,          0x02 );
LENS_OFFSET_CHECK( prop_lens_dynamic_data, AVMAX,        0x04 );
LENS_OFFSET_CHECK( prop_lens_dynamic_data, FL,           0x40 );
LENS_OFFSET_CHECK( prop_lens_dynamic_data, focus_far,    0x44 );
LENS_OFFSET_CHECK( prop_lens_dynamic_data, focus_near,   0x46 );
LENS_OFFSET_CHECK( prop_lens_dynamic_data, focusPos,     0x4A );
LENS_OFFSET_CHECK( prop_lens_dynamic_data, st2,          0x5C );
LENS_OFFSET_CHECK( prop_lens_dynamic_data, st3,          0x5D );
LENS_OFFSET_CHECK( prop_lens_dynamic_data, HRFL,         0x7A );
LENS_OFFSET_CHECK( prop_lens_static_data,  lens_id,      0x28 );
LENS_OFFSET_CHECK( prop_lens_static_data,  fl_wide,      0x2C );
LENS_OFFSET_CHECK( prop_lens_static_data,  fl_tele,      0x2E );
LENS_OFFSET_CHECK( prop_lens_static_data,  lens_serial,  0x30 );
LENS_OFFSET_CHECK( prop_lens_static_data,  extender_id,  0x4D );
LENS_OFFSET_CHECK( prop_lens_static_data,  lens_name,    0x5A );
LENS_OFFSET_CHECK( prop_lens_static_data,  lens_is_funct_exists, 0xE9 );
LENS_OFFSET_CHECK( prop_lens_static_data,  zoom_pos_size, 0xF4 );
LENS_OFFSET_CHECK( prop_lens_static_data,  dlAdp1_id,    0x1AC );
LENS_OFFSET_CHECK( prop_lens_static_data,  high_res_focal_length_tele, 0x1C0 );

#undef LENS_OFFSET_CHECK

#endif
