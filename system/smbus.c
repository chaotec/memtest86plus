// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2004-2022 Samuel Demeulemeester
//

#include "display.h"

#include "io.h"
#include "tsc.h"
#include "pci.h"
#include "unistd.h"

#include "cpuinfo.h"
#include "smbus.h"
#include "smbios.h"
#include "jedec_id.h"

#define LINE_SPD        13
#define MAX_SPD_SLOT    8

ram_info ram = { 0, 0, 0, 0, 0, "N/A"};

int smbdev, smbfun;
unsigned short smbusbase;

static int8_t spd_page = -1;
static int8_t last_adr = -1;

static spd_info parse_spd_rdram (uint8_t smb_idx, uint8_t slot_idx);
static spd_info parse_spd_ddr (uint8_t smb_idx, uint8_t slot_idx);
static spd_info parse_spd_ddr2(uint8_t smb_idx, uint8_t slot_idx);
static spd_info parse_spd_ddr3(uint8_t smb_idx, uint8_t slot_idx);
static spd_info parse_spd_ddr4(uint8_t smb_idx, uint8_t slot_idx);
static spd_info parse_spd_ddr5(uint8_t smb_idx, uint8_t slot_idx);
static void print_spdi(spd_info spdi, uint8_t lidx);

static int find_smb_controller(void);

static void fch_zen_get_smb(void);

static void ich5_get_smb(void);
static uint8_t ich5_process(void);
static uint8_t ich5_read_spd_byte(uint8_t adr, uint16_t cmd);

static const struct pci_smbus_controller smbcontrollers[] = {
    {0x8086, 0x2413, "82801AA (ICH)",           ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x2423, "82801AB (ICH)",           ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x2443, "82801BA (ICH2)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x2483, "82801CA (ICH3)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x24C3, "82801DB (ICH4)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x24D3, "82801E (ICH5)",           ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x25A4, "6300ESB",                 ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x266A, "82801F (ICH6)",           ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x269B, "6310ESB/6320ESB",         ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x27DA, "82801G (ICH7)",           ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x283E, "82801H (ICH8)",           ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x2930, "82801I (ICH9)",           ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x5032, "EP80579 (Tolapai)",       ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x3A30, "ICH10",                   ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x3A60, "ICH10",                   ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x3B30, "5/3400 Series (PCH)",     ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1C22, "6 Series (PCH)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1D22, "Patsburg (PCH)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1D70, "Patsburg (PCH) IDF",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1D71, "Patsburg (PCH) IDF",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1D72, "Patsburg (PCH) IDF",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x2330, "DH89xxCC (PCH)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1E22, "Panther Point (PCH)",     ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x8C22, "Lynx Point (PCH)",        ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x9C22, "Lynx Point-LP (PCH)",     ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1F3C, "Avoton (SOC)",            ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x8D22, "Wellsburg (PCH)",         ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x8D7D, "Wellsburg (PCH) MS",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x8D7E, "Wellsburg (PCH) MS",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x8D7F, "Wellsburg (PCH) MS",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x23B0, "Coleto Creek (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x8CA2, "Wildcat Point (PCH)",     ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x9CA2, "Wildcat Point-LP (PCH)",  ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x0F12, "BayTrail (SOC)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x2292, "Braswell (SOC)",          ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA123, "Sunrise Point-H (PCH) ",  ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x9D23, "Sunrise Point-LP (PCH)",  ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x19DF, "Denverton  (SOC)",        ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x1BC9, "Emmitsburg (PCH)",        ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA1A3, "Lewisburg (PCH)",         ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA223, "Lewisburg Super (PCH)",   ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA2A3, "Kaby Lake (PCH-H)",       ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x31D4, "Gemini Lake (SOC)",       ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA323, "Cannon Lake-H (PCH)",     ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x9DA3, "Cannon Lake-LP (PCH)",    ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x18DF, "Cedar Fork (PCH)",        ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x34A3, "Ice Lake-LP (PCH)",       ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x38A3, "Ice Lake-N (PCH)",        ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x02A3, "Comet Lake (PCH)",        ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x06A3, "Comet Lake-H (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x4B23, "Elkhart Lake (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA0A3, "Tiger Lake-LP (PCH)",     ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x43A3, "Tiger Lake-H (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x4DA3, "Jasper Lake (SOC)",       ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0xA3A3, "Comet Lake-V (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x7AA3, "Alder Lake-S (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x51A3, "Alder Lake-P (PCH)",      ich5_get_smb, ich5_read_spd_byte},
    {0x8086, 0x54A3, "Alder Lake-M (PCH)",      ich5_get_smb, ich5_read_spd_byte},

     // AMD SMBUS
     {0x1022, 0x780B, "AMD FCH", NULL, NULL},
     {0x1022, 0x790B, "AMD FCH (Zen)",          fch_zen_get_smb, ich5_read_spd_byte},
     {0, 0, "", NULL, NULL}
};

void print_smbus_startup_info(void) {

    int8_t index;
    uint8_t spdidx = 0, spd_line_idx = 0;

    spd_info curspd;
    ram.freq = 0;
    curspd.isValid = false;

    index = find_smb_controller();

    if (index == -1) {
        return;
    }

    smbcontrollers[index].get_adr();

    for (spdidx = 0; spdidx < MAX_SPD_SLOT; spdidx++) {

        if (get_spd(index, spdidx, 0) != 0xFF) {
            switch(get_spd(index, spdidx, 2))
            {
                default:
                    continue;
                case 0x12: // DDR5
                    curspd = parse_spd_ddr5(index, spdidx);
                    break;
                case 0x0C: // DDR4
                    curspd = parse_spd_ddr4(index, spdidx);
                    break;
                case 0x0B: // DDR3
                    curspd = parse_spd_ddr3(index, spdidx);
                    break;
                case 0x08: // DDR2
                    curspd = parse_spd_ddr2(index, spdidx);
                    break;
                case 0x07: // DDR
                    curspd = parse_spd_ddr(index, spdidx);
                    break;
                case 0x01: // RAMBUS - RDRAM
                    if (get_spd(index, spdidx, 1) == 8) {
                        curspd = parse_spd_rdram(index, spdidx);
                    }
                    break;
            }

            if (curspd.isValid) {
                if(spd_line_idx == 0) {
                    prints(LINE_SPD-2, 0, "Memory SPD Informations");
                    prints(LINE_SPD-1, 0, "-----------------------");
                }

                print_spdi(curspd, spd_line_idx);
                spd_line_idx++;
            }
        }
    }
}

static void print_spdi(spd_info spdi, uint8_t lidx)
{
    uint8_t curcol;
    uint16_t i;

    // Print Slot Index, Module Size, type & Max frequency (Jedec or XMP)
    curcol = printf(LINE_SPD+lidx, 0, " - Slot %i : %kB %s-%i",
                    spdi.slot_num,
                    spdi.module_size * 1024,
                    spdi.type,
                    spdi.freq);

    // Print ECC status
    if (spdi.hasECC) {
        curcol = prints(LINE_SPD+lidx, curcol, " ECC");
    }

    // Print XMP/EPP Status
    if (spdi.XMP > 0 && spdi.XMP < 20) {
        curcol = prints(LINE_SPD+lidx, curcol, " XMP");
    } else if (spdi.XMP == 20) {
        curcol = prints(LINE_SPD+lidx, curcol, " EPP");
    }

    // Print Manufacturer from JEDEC106
    for (i = 0; i < JEP106_CNT ; i++) {

        if (spdi.jedec_code == jep106[i].jedec_code) {
            curcol = printf(LINE_SPD+lidx, curcol, " - %s", jep106[i].name);
            break;
        }
    }

    // If not present in JEDEC106, display raw JEDEC ID
    if (i == JEP106_CNT && spdi.jedec_code != 0) {
        curcol = printf(LINE_SPD+lidx, curcol, " - Unknown (0x%x)", spdi.jedec_code);
    }

    // Print SKU
    for(i = 0; i < spdi.sku_len; i++) {
        printc(LINE_SPD+lidx, ++curcol, spdi.sku[i]);
    }

    // Print Manufacturing date (only if valid)
    if (curcol <= 72 && spdi.fab_year > 1 && spdi.fab_year < 32 && spdi.fab_week < 53) {
        curcol = printf(LINE_SPD+lidx, curcol, " (W%02i'%02i)", spdi.fab_week, spdi.fab_year);
    }

    // Populate global ram var
    ram.type = spdi.type;
    if (ram.freq == 0 || ram.freq > spdi.freq) {
        ram.freq = spdi.freq;
    }
    if (ram.tCL < spdi.tCL) {
        ram.tCL  = spdi.tCL;
        ram.tRCD = spdi.tRCD;
        ram.tRP  = spdi.tRP;
        ram.tRAS = spdi.tRAS;
    }
}

static spd_info parse_spd_ddr5(uint8_t smb_idx, uint8_t slot_idx)
{
    spd_info spdi;

    spdi.isValid = false;
    spdi.type = "DDR5";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;
    spdi.module_size = 0;

    // Compute module size for symmetric & assymetric configuration
    for (int sbyte_adr = 1; sbyte_adr <= 2; sbyte_adr++) {
        uint32_t cur_rank = 0;
        uint8_t sbyte =  get_spd(smb_idx, slot_idx, sbyte_adr * 4);

        // SDRAM Density per die
        switch (sbyte & 0x1F)
        {
          default:
            break;
          case 0b00001:
            cur_rank = 512;
            break;
          case 0b00010:
            cur_rank = 1024;
            break;
          case 0b00011:
            cur_rank = 1536;
            break;
          case 0b00100:
            cur_rank = 2048;
            break;
          case 0b00101:
            cur_rank = 3072;
            break;
          case 0b00110:
            cur_rank = 4096;
            break;
          case 0b00111:
            cur_rank = 6144;
            break;
          case 0b01000:
            cur_rank = 8192;
            break;
        }

        // Die per package
        if ((sbyte >> 5) > 1 && (sbyte >> 5) <= 5) {
            cur_rank *=  1 << (((sbyte >> 5) & 7) - 1);
        }

        sbyte = get_spd(smb_idx, slot_idx, 235);
        spdi.hasECC = (((sbyte >> 3) & 3) > 0);

        // Channels per DIMM
        if (((sbyte >> 5) & 3) == 1) {
            cur_rank *= 2;
        }

        // Primary Bus Width per Channel
        cur_rank *= 1 << ((sbyte & 3) + 3);

        // I/O Width
        sbyte = get_spd(smb_idx, slot_idx, (sbyte_adr * 4) + 2);
        cur_rank /= 1 << (((sbyte >> 5) & 3) + 2);

        // Add current rank to total package size
        spdi.module_size += cur_rank;

        // If not Asymmetrical, don't process the second rank
        if ((get_spd(smb_idx, slot_idx, 234) >> 6) == 0) {
            break;
        }
    }

    // Compute Frequency (including XMP)
    uint16_t tCK, tCKtmp, tns;
    int xmp_offset = 0;

    spdi.XMP = ((get_spd(smb_idx, slot_idx, 640) == 0x0C && get_spd(smb_idx, slot_idx, 641) == 0x4A)) ? 3 : 0;

    if (spdi.XMP == 3) {
        // XMP 3.0 (enumerate all profiles to find the fastest)
        tCK = 0;
        for (int offset = 0; offset < 3*64; offset += 64) {
            tCKtmp = get_spd(smb_idx, slot_idx, 710 + offset) << 8 |
                     get_spd(smb_idx, slot_idx, 709 + offset);

            if (tCKtmp != 0 && (tCK == 0 || tCKtmp < tCK)) {
                xmp_offset = offset;
                tCK = tCKtmp;
            }
        }
    } else {
        // JEDEC
        tCK = get_spd(smb_idx, slot_idx, 21) << 8 |
              get_spd(smb_idx, slot_idx, 20);
    }

    if (tCK == 0) {
        return spdi;
    }

    spdi.freq = (float)(1.0f / tCK * 2.0f * 1000.0f * 1000.0f);
    spdi.freq = (spdi.freq + 50) / 100 * 100;

    // Module Timings
    if (spdi.XMP == 3) {
        // ------------------
        // XMP Specifications
        // ------------------

        // CAS# Latency
        tns  = (uint16_t)get_spd(smb_idx, slot_idx, 718 + xmp_offset) << 8 |
               (uint16_t)get_spd(smb_idx, slot_idx, 717 + xmp_offset);
        spdi.tCL = (uint16_t)(tns/tCK + 0.5f);

        // RAS# to CAS# Latency
        tns  = (uint16_t)get_spd(smb_idx, slot_idx, 720 + xmp_offset) << 8 |
               (uint16_t)get_spd(smb_idx, slot_idx, 719 + xmp_offset);
        spdi.tRCD = (uint16_t)(tns/tCK + 0.5f);

        // RAS# Precharge
        tns  = (uint16_t)get_spd(smb_idx, slot_idx, 722 + xmp_offset) << 8 |
               (uint16_t)get_spd(smb_idx, slot_idx, 721 + xmp_offset);
        spdi.tRP= (uint16_t)(tns/tCK + 0.5f);

        // Row Active Time
        tns  = (uint16_t)get_spd(smb_idx, slot_idx, 724 + xmp_offset) << 8 |
               (uint16_t)get_spd(smb_idx, slot_idx, 723 + xmp_offset);
        spdi.tRAS = (uint16_t)(tns/tCK + 0.5f);

        // Row Cycle Time
        tns  = (uint16_t)get_spd(smb_idx, slot_idx, 726 + xmp_offset) << 8 |
               (uint16_t)get_spd(smb_idx, slot_idx, 725 + xmp_offset);
        spdi.tRC = (uint16_t)(tns/tCK + 0.5f);
    } else {
        // --------------------
        // JEDEC Specifications
        // --------------------

        // CAS# Latency
        tns  = (uint16_t)get_spd(smb_idx, slot_idx, 31) << 8 |
               (uint16_t)get_spd(smb_idx, slot_idx, 30);
        spdi.tCL = (uint16_t)(tns/tCK + 0.5f);

        // RAS# to CAS# Latency
        tns  = (uint16_t)get_spd(smb_idx, slot_idx, 33) << 8 |
               (uint16_t)get_spd(smb_idx, slot_idx, 32);
        spdi.tRCD = (uint16_t)(tns/tCK + 0.5f);

        // RAS# Precharge
        tns  = (uint16_t)get_spd(smb_idx, slot_idx, 35) << 8 |
               (uint16_t)get_spd(smb_idx, slot_idx, 34);
        spdi.tRP= (uint16_t)(tns/tCK + 0.5f);

        // Row Active Time
        tns  = (uint16_t)get_spd(smb_idx, slot_idx, 37) << 8 |
               (uint16_t)get_spd(smb_idx, slot_idx, 36);
        spdi.tRAS = (uint16_t)(tns/tCK + 0.5f);

        // Row Cycle Time
        tns  = (uint16_t)get_spd(smb_idx, slot_idx, 39) << 8 |
               (uint16_t)get_spd(smb_idx, slot_idx, 38);
        spdi.tRC = (uint16_t)(tns/tCK + 0.5f);
    }

    // Module manufacturer
    spdi.jedec_code = (get_spd(smb_idx, slot_idx, 512) & 0x1F) << 8;
    spdi.jedec_code |= get_spd(smb_idx, slot_idx, 513) & 0x7F;

    // Module SKU
    uint8_t sku_byte;
    for (int j = 0; j <= 29; j++) {
        sku_byte = get_spd(smb_idx, slot_idx, 521+j);

        if (sku_byte <= 0x20 && j > 1 && spdi.sku[j-1] <= 0x20) {
            break;
        } else {
            spdi.sku[j] = sku_byte;
            spdi.sku_len++;
        }
    }

    // Week & Date (BCD to Int)
    uint8_t bcd = get_spd(smb_idx, slot_idx, 515);
    spdi.fab_year =  bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 516);
    spdi.fab_week =  bcd - 6 * (bcd >> 4);

    spdi.isValid = true;

    return spdi;
}

static spd_info parse_spd_ddr4(uint8_t smb_idx, uint8_t slot_idx)
{
    spd_info spdi;

    spdi.type = "DDR4";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;

    // Compute module size in MB with shifts
    spdi.module_size = 1 << (
                             ((get_spd(smb_idx, slot_idx, 4) & 0xF) + 5)  +
                             ((get_spd(smb_idx, slot_idx, 13) & 0x7) + 3)  -
                             ((get_spd(smb_idx, slot_idx, 12) & 0x7) + 2)  +
                             ((get_spd(smb_idx, slot_idx, 12) >> 3) & 0x7) +
                             ((get_spd(smb_idx, slot_idx, 6) >> 4) & 0x7)
                            );

    spdi.hasECC = (((get_spd(smb_idx, slot_idx, 13) >> 3) & 1) == 1);

    // Module max clock
    float tns, tckns, ramfreq;

    if (get_spd(smb_idx, slot_idx, 384) == 0x0C && get_spd(smb_idx, slot_idx, 385) == 0x4A) {
        // Max XMP
        tckns = (uint8_t)get_spd(smb_idx, slot_idx, 396) * 0.125f +
                (int8_t)get_spd(smb_idx, slot_idx, 431)  * 0.001f;

        ramfreq =  1.0f / tckns * 2.0f * 1000.0f;

        spdi.freq = (ramfreq+50)/100;
        spdi.freq *= 100;

        spdi.XMP = 2;

    } else {
        // Max JEDEC
        tckns = (uint8_t)get_spd(smb_idx, slot_idx, 18) * 0.125f +
                (int8_t)get_spd(smb_idx, slot_idx, 125) * 0.001f;

        ramfreq = 1.0f / tckns * 2.0f * 1000.0f;
        spdi.freq = (uint16_t)ramfreq;

        spdi.XMP = 0;
    }

    // Module Timings
    if (spdi.XMP == 2) {
        // ------------------
        // XMP Specifications
        // ------------------

        // CAS# Latency
        tns  = (uint8_t)get_spd(smb_idx, slot_idx, 401) * 0.125f +
               (int8_t)get_spd(smb_idx, slot_idx, 430)  * 0.001f;
        spdi.tCL = (uint16_t)(tns/tckns);

        // RAS# to CAS# Latency
        tns  = (uint8_t)get_spd(smb_idx, slot_idx, 402) * 0.125f +
               (int8_t)get_spd(smb_idx, slot_idx, 429)  * 0.001f;
        spdi.tRCD = (uint16_t)(tns/tckns);

        // RAS# Precharge
        tns  = (uint8_t)get_spd(smb_idx, slot_idx, 403) * 0.125f +
               (int8_t)get_spd(smb_idx, slot_idx, 428)  * 0.001f;
        spdi.tRP= (uint16_t)(tns/tckns);

        // Row Active Time
        tns = (uint8_t)get_spd(smb_idx, slot_idx, 405) * 0.125f +
              (int8_t)get_spd(smb_idx, slot_idx, 427)  * 0.001f  +
              (uint8_t)(get_spd(smb_idx, slot_idx, 404) & 0x0F) * 32.0f;
        spdi.tRAS = (uint16_t)(tns/tckns);

        // Row Cycle Time
        tns = (uint8_t)get_spd(smb_idx, slot_idx, 406) * 0.125f +
              (uint8_t)(get_spd(smb_idx, slot_idx, 404) >> 4) * 32.0f;
        spdi.tRC = (uint16_t)(tns/tckns);
    } else {
        // --------------------
        // JEDEC Specifications
        // --------------------

        // CAS# Latency
        tns  = (uint8_t)get_spd(smb_idx, slot_idx, 24) * 0.125f +
               (int8_t)get_spd(smb_idx, slot_idx, 123) * 0.001f;
        spdi.tCL = (uint16_t)(tns/tckns);

        // RAS# to CAS# Latency
        tns  = (uint8_t)get_spd(smb_idx, slot_idx, 25) * 0.125f +
               (int8_t)get_spd(smb_idx, slot_idx, 122) * 0.001f;
        spdi.tRCD = (uint16_t)(tns/tckns);

        // RAS# Precharge
        tns  = (uint8_t)get_spd(smb_idx, slot_idx, 26) * 0.125f +
               (int8_t)get_spd(smb_idx, slot_idx, 121) * 0.001f;
        spdi.tRP= (uint16_t)(tns/tckns);

        // Row Active Time
        tns = (uint8_t)get_spd(smb_idx, slot_idx, 28) * 0.125f +
              (uint8_t)(get_spd(smb_idx, slot_idx, 27) & 0x0F) * 32.0f;
        spdi.tRAS = (uint16_t)(tns/tckns);

        // Row Cycle Time
        tns = (uint8_t)get_spd(smb_idx, slot_idx, 29) * 0.125f +
              (uint8_t)(get_spd(smb_idx, slot_idx, 27) >> 4) * 32.0f;
        spdi.tRC = (uint16_t)(tns/tckns);
    }

    // Module manufacturer
    spdi.jedec_code = (get_spd(smb_idx, slot_idx, 320) & 0x1F) << 8;
    spdi.jedec_code |= get_spd(smb_idx, slot_idx, 321) & 0x7F;

    // Module SKU
    uint8_t sku_byte;
    for (int j = 0; j <= 20; j++) {
        sku_byte = get_spd(smb_idx, slot_idx, 329+j);

        if (sku_byte <= 0x20 && j > 1 && spdi.sku[j-1] <= 0x20) {
            spdi.sku_len--;
            break;
        } else {
            spdi.sku[j] = sku_byte;
            spdi.sku_len++;
        }
    }

    // Week & Date (BCD to Int)
    uint8_t bcd = get_spd(smb_idx, slot_idx, 323);
    spdi.fab_year =  bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 324);
    spdi.fab_week =  bcd - 6 * (bcd >> 4);

    spdi.isValid = true;

    return spdi;
}

static spd_info parse_spd_ddr3(uint8_t smb_idx, uint8_t slot_idx)
{
    spd_info spdi;

    spdi.type = "DDR3";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;
    spdi.XMP = 0;

    // Compute module size in MB with shifts
    spdi.module_size = 1 << (
                             ((get_spd(smb_idx, slot_idx, 4) & 0xF) + 5)  +
                             ((get_spd(smb_idx, slot_idx, 8) & 0x7) + 3)  -
                             ((get_spd(smb_idx, slot_idx, 7) & 0x7) + 2)  +
                             ((get_spd(smb_idx, slot_idx, 7) >> 3) & 0x7)
                            );

    spdi.hasECC = (((get_spd(smb_idx, slot_idx, 8) >> 3) & 1) == 1);

    uint8_t tck = get_spd(smb_idx, slot_idx, 12);

    if (get_spd(smb_idx, slot_idx, 176) == 0x0C && get_spd(smb_idx, slot_idx, 177) == 0x4A) {
        tck = get_spd(smb_idx, slot_idx, 186);
        spdi.XMP = 1;
    }

    // Module jedec speed
    switch (tck) {
        default:
            spdi.freq = 0;
            break;
        case 20:
            spdi.freq = 800;
            break;
        case 15:
            spdi.freq = 1066;
            break;
        case 12:
            spdi.freq = 1333;
            break;
        case 10:
            spdi.freq = 1600;
            break;
        case 9:
            spdi.freq = 1866;
            break;
        case 8:
            spdi.freq = 2133;
            break;
        case 7:
            spdi.freq = 2400;
            break;
        case 6:
            spdi.freq = 2666;
            break;
    }

    // Module Timings
    float tckns, tns;
    if (spdi.XMP == 1) {
        // ------------------
        // XMP Specifications
        // ------------------
        tckns = get_spd(smb_idx, slot_idx, 186);

        // CAS# Latency
        tns  = get_spd(smb_idx, slot_idx, 187);
        spdi.tCL = (uint16_t)(tns/tckns);

        // RAS# to CAS# Latency
        tns  = get_spd(smb_idx, slot_idx, 192);
        spdi.tRCD = (uint16_t)(tns/tckns);

        // RAS# Precharge
        tns  = get_spd(smb_idx, slot_idx, 191);
        spdi.tRP= (uint16_t)(tns/tckns);

        // Row Active Time
        tns  = (uint16_t)(get_spd(smb_idx, slot_idx, 194) & 0xF0) << 4 |
               get_spd(smb_idx, slot_idx, 195);
               ;
        spdi.tRAS = (uint16_t)(tns/tckns);

        // Row Cycle Time
        tns  = (uint16_t)(get_spd(smb_idx, slot_idx, 194) & 0x0F) << 8 |
               get_spd(smb_idx, slot_idx, 196);
        spdi.tRC = (uint16_t)(tns/tckns);
    } else {
        // --------------------
        // JEDEC Specifications
        // --------------------
        tckns = (uint8_t)get_spd(smb_idx, slot_idx, 12) * 0.125f +
                (int8_t)get_spd(smb_idx, slot_idx, 134) * 0.001f;

        // CAS# Latency
        tns  = (uint8_t)get_spd(smb_idx, slot_idx, 16) * 0.125f +
               (int8_t)get_spd(smb_idx, slot_idx, 35) * 0.001f;
        spdi.tCL = (uint16_t)(tns/tckns);

        // RAS# to CAS# Latency
        tns  = (uint8_t)get_spd(smb_idx, slot_idx, 18) * 0.125f +
               (int8_t)get_spd(smb_idx, slot_idx, 36) * 0.001f;
        spdi.tRCD = (uint16_t)(tns/tckns);

        // RAS# Precharge
        tns  = (uint8_t)get_spd(smb_idx, slot_idx, 20) * 0.125f +
               (int8_t)get_spd(smb_idx, slot_idx, 37) * 0.001f;
        spdi.tRP= (uint16_t)(tns/tckns);

        // Row Active Time
        tns = (uint8_t)get_spd(smb_idx, slot_idx, 22) * 0.125f +
              (uint8_t)(get_spd(smb_idx, slot_idx, 21) & 0x0F) * 32.0f;
        spdi.tRAS = (uint16_t)(tns/tckns);

        // Row Cycle Time
        tns = (uint8_t)get_spd(smb_idx, slot_idx, 23) * 0.125f +
              (uint8_t)(get_spd(smb_idx, slot_idx, 21) >> 4) * 32.0f;
        spdi.tRC = (uint16_t)(tns/tckns);
    }

    // Module manufacturer
    spdi.jedec_code= (get_spd(smb_idx, slot_idx, 117) & 0x1F) << 8;
    spdi.jedec_code |= get_spd(smb_idx, slot_idx, 118) & 0x7F;

    // Module SKU
    uint8_t sku_byte;
    for (int j = 0; j <= 20; j++) {
        sku_byte = get_spd(smb_idx, slot_idx, 128+j);

        if(sku_byte <= 0x20 && j > 0 && spdi.sku[j-1] <= 0x20) {
            spdi.sku_len--;
            break;
        } else {
            spdi.sku[j] = sku_byte;
            spdi.sku_len++;
        }
    }

    uint8_t bcd = get_spd(smb_idx, slot_idx, 120);
    spdi.fab_year =  bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 121);
    spdi.fab_week =  bcd - 6 * (bcd >> 4);

    spdi.isValid = true;

    return spdi;
}

static spd_info parse_spd_ddr2(uint8_t smb_idx, uint8_t slot_idx)
{
    spd_info spdi;

    spdi.type = "DDR2";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;
    spdi.XMP = 0;

    // Compute module size in MB
    switch (get_spd(smb_idx, slot_idx, 31)) {
        case 1:
            spdi.module_size = 1024;
            break;
        case 2:
            spdi.module_size = 2048;
            break;
        case 4:
            spdi.module_size = 4096;
            break;
        case 8:
            spdi.module_size = 8192;
            break;
        case 16:
            spdi.module_size = 16384;
            break;
        case 32:
            spdi.module_size = 128;
            break;
        case 64:
            spdi.module_size = 256;
            break;
        default:
        case 128:
            spdi.module_size = 512;
            break;
    }

    spdi.module_size *= (get_spd(smb_idx, slot_idx, 5) & 7) + 1;

    spdi.hasECC = ((get_spd(smb_idx, slot_idx, 11) >> 1) == 1);

    float tckns, tns;
    uint8_t tbyte;

    // Module EPP Detection (we only support Full profiles
    uint8_t epp_offset = 0;
    if (get_spd(smb_idx, slot_idx, 99) == 0x6D && get_spd(smb_idx, slot_idx, 102) == 0xB1) {
        epp_offset = (get_spd(smb_idx, slot_idx, 103) & 0x3) * 12;
        tbyte = get_spd(smb_idx, slot_idx, 109 + epp_offset);
        spdi.XMP = 20;
    } else {
        tbyte = get_spd(smb_idx, slot_idx, 9);
    }

    // Module speed
    tckns = (tbyte & 0xF0) >> 4;
    tbyte &= 0xF;

    if (tbyte < 10) {
        tckns += (tbyte & 0xF) * 0.1f;
    } else if (tbyte == 10) {
        tckns += 0.25f;
    } else if (tbyte == 11) {
        tckns += 0.33f;
    } else if (tbyte == 12) {
        tckns += 0.66f;
    } else if (tbyte == 13) {
        tckns += 0.75f;
    } else if (tbyte == 14) { // EPP Specific
        tckns += 0.875f;
    }

    spdi.freq = (float)(1.0f / tckns * 1000.0f * 2.0f);

    if (spdi.XMP == 20) {
        // Module Timings (EPP)
        // CAS# Latency
        tbyte = get_spd(smb_idx, slot_idx, 110 + epp_offset);
        for (int shft = 0; shft < 7; shft++) {
            if ((tbyte >> shft) & 1) {
                spdi.tCL = shft;
                break;
            }
        }

        // RAS# to CAS# Latency
        tbyte = get_spd(smb_idx, slot_idx, 111 + epp_offset);
        tns  = ((tbyte & 0xFC) >> 2) + (tbyte & 0x3) * 0.25f;
        spdi.tRCD = (uint16_t)(tns/tckns);

        // RAS# Precharge
        tbyte = get_spd(smb_idx, slot_idx, 112 + epp_offset);
        tns  = ((tbyte & 0xFC) >> 2) + (tbyte & 0x3) * 0.25f;
        spdi.tRP= (uint16_t)(tns/tckns);

        // Row Active Time
        tns = get_spd(smb_idx, slot_idx, 113 + epp_offset);
        spdi.tRAS = (uint16_t)(tns/tckns);

        // Row Cycle Time
        tns = 0;
    } else {
        // Module Timings (JEDEC)
        // CAS# Latency
        tbyte = get_spd(smb_idx, slot_idx, 18);
        for (int shft = 0; shft < 7; shft++) {
            if ((tbyte >> shft) & 1) {
                spdi.tCL = shft;
                break;
            }
        }

        // RAS# to CAS# Latency
        tbyte = get_spd(smb_idx, slot_idx, 29);
        tns  = ((tbyte & 0xFC) >> 2) + (tbyte & 0x3) * 0.25f;
        spdi.tRCD = (uint16_t)(tns/tckns);

        // RAS# Precharge
        tbyte = get_spd(smb_idx, slot_idx, 27);
        tns  = ((tbyte & 0xFC) >> 2) + (tbyte & 0x3) * 0.25f;
        spdi.tRP= (uint16_t)(tns/tckns);

        // Row Active Time
        tns = get_spd(smb_idx, slot_idx, 30);
        spdi.tRAS = (uint16_t)(tns/tckns);

        // Row Cycle Time
        tns = 0;
    }

    // Module manufacturer
    uint8_t contcode;
    for (contcode = 64; contcode < 72; contcode++) {
        if (get_spd(smb_idx, slot_idx, contcode) != 0x7F) {
            break;
        }
    }

    spdi.jedec_code = (contcode - 64) << 8;
    spdi.jedec_code |= get_spd(smb_idx, slot_idx, contcode) & 0x7F;

    // Module SKU
    uint8_t sku_byte;
    for (int j = 0; j < 18; j++) {
        sku_byte = get_spd(smb_idx, slot_idx, 73 + j);

        if (sku_byte <= 0x20 && j > 0 && spdi.sku[j - 1] <= 0x20) {
            spdi.sku_len--;
            break;
        } else {
            spdi.sku[j] = sku_byte;
            spdi.sku_len++;
        }
    }

    uint8_t bcd = get_spd(smb_idx, slot_idx, 94);
    spdi.fab_year = bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 93);
    spdi.fab_week = bcd - 6 * (bcd >> 4);

    spdi.isValid = true;

    return spdi;
}

static spd_info parse_spd_ddr(uint8_t smb_idx, uint8_t slot_idx)
{
    spd_info spdi;

    spdi.type = "DDR";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;
    spdi.XMP = 0;

    // Compute module size in MB
    switch (get_spd(smb_idx, slot_idx, 31)) {
        case 1:
            spdi.module_size = 1024;
            break;
        case 2:
            spdi.module_size = 2048;
            break;
        case 4:
            spdi.module_size = 4096;
            break;
        case 8:
            spdi.module_size = 32;
            break;
        case 16:
            spdi.module_size = 64;
            break;
        case 32:
            spdi.module_size = 128;
            break;
        case 64:
            spdi.module_size = 256;
            break;
        case 128:
            spdi.module_size = 512;
            break;
        default: // we don't support asymetric banking
            spdi.module_size = 0;
            break;
    }

    spdi.module_size *= get_spd(smb_idx, slot_idx, 5);

    spdi.hasECC = ((get_spd(smb_idx, slot_idx, 11) >> 1) == 1);

    // Module speed
    float tns, tckns;
    uint8_t spd_byte9 = get_spd(smb_idx, slot_idx, 9);
    tckns = (spd_byte9 >> 4) + (spd_byte9 & 0xF) * 0.1f;

    spdi.freq = (uint16_t)(1.0f / tckns* 1000.0f * 2.0f );

    // Module Timings
    uint8_t spd_byte18 = get_spd(smb_idx, slot_idx, 18);
    for (int shft = 0; shft < 7; shft++) {
        if ((spd_byte18 >> shft) & 1) {
            spdi.tCL = 1.0f + shft * 0.5f; // TODO: .5 CAS
            break;
        }
    }

    tns  = (get_spd(smb_idx, slot_idx, 29) >> 2) +
           (get_spd(smb_idx, slot_idx, 29) & 0x3) * 0.25f;
    spdi.tRCD = (uint16_t)(tns/tckns);

    tns  = (get_spd(smb_idx, slot_idx, 27) >> 2) +
           (get_spd(smb_idx, slot_idx, 27) & 0x3) * 0.25f;
    spdi.tRP= (uint16_t)(tns/tckns);

    spdi.tRAS = (uint16_t)(get_spd(smb_idx, slot_idx, 30)/tckns);
    spdi.tRC = 0;

    // Module manufacturer
    uint8_t contcode;
    for (contcode = 64; contcode < 72; contcode++) {
        if (get_spd(smb_idx, slot_idx, contcode) != 0x7F) {
            break;
        }
    }

    spdi.jedec_code = (contcode - 64) << 8;
    spdi.jedec_code |= get_spd(smb_idx, slot_idx, contcode) & 0x7F;

    // Module SKU
    uint8_t sku_byte;
    for (int j = 0; j < 18; j++) {
        sku_byte = get_spd(smb_idx, slot_idx, 73 + j);

        if (sku_byte <= 0x20 && j > 0 && spdi.sku[j - 1] <= 0x20) {
            spdi.sku_len--;
            break;
        } else {
            spdi.sku[j] = sku_byte;
            spdi.sku_len++;
        }
    }

    uint8_t bcd = get_spd(smb_idx, slot_idx, 93);
    spdi.fab_year = bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 94);
    spdi.fab_week = bcd - 6 * (bcd >> 4);

    spdi.isValid = true;

    return spdi;
}

static spd_info parse_spd_rdram(uint8_t smb_idx, uint8_t slot_idx)
{
    spd_info spdi;

    spdi.isValid = false;
    spdi.type = "RDRAM";
    spdi.slot_num = slot_idx;
    spdi.sku_len = 0;
    spdi.XMP = 0;

    // Compute module size in MB
    uint8_t tbyte = get_spd(smb_idx, slot_idx, 5);
    switch(tbyte) {
        case 0x84:
            spdi.module_size = 8;
            break;
        case 0xC5:
            spdi.module_size = 16;
            break;
        default:
            return spdi;
    }

    spdi.module_size *= get_spd(smb_idx, slot_idx, 99);

    tbyte = get_spd(smb_idx, slot_idx, 4);
    if(tbyte > 0x96) {
        spdi.module_size *= 1 + (((tbyte & 0xF0) >> 4) - 9) + ((tbyte & 0xF) - 6);
    }

    spdi.hasECC = (get_spd(smb_idx, slot_idx, 100) == 0x12) ? true : false;

    // Module speed
    tbyte = get_spd(smb_idx, slot_idx, 15);
    switch(tbyte) {
        case 0x1A:
            spdi.freq = 600;
            break;
        case 0x15:
            spdi.freq = 711;
            break;
        case 0x13:
            spdi.freq = 800;
            break;
        case 0xe:
            spdi.freq = 1066;
            break;
        case 0xc:
            spdi.freq = 1200;
            break;
        default:
            return spdi;
    }

    // Module Timings
    spdi.tCL = get_spd(smb_idx, slot_idx, 14);
    spdi.tRCD = get_spd(smb_idx, slot_idx, 12);
    spdi.tRP= get_spd(smb_idx, slot_idx, 10);
    spdi.tRAS = get_spd(smb_idx, slot_idx, 11);
    spdi.tRC = 0;

    // Module manufacturer
    uint8_t contcode;
    for (contcode = 64; contcode < 72; contcode++) {
        if (get_spd(smb_idx, slot_idx, contcode) != 0x7F) {
            break;
        }
    }

    spdi.jedec_code = (contcode - 64) << 8;
    spdi.jedec_code |= get_spd(smb_idx, slot_idx, contcode) & 0x7F;

    // Module SKU
    uint8_t sku_byte;
    for (int j = 0; j < 18; j++) {
        sku_byte = get_spd(smb_idx, slot_idx, 73 + j);

        if (sku_byte <= 0x20 && j > 0 && spdi.sku[j - 1] <= 0x20) {
            spdi.sku_len--;
            break;
        } else {
            spdi.sku[j] = sku_byte;
            spdi.sku_len++;
        }
    }

    uint8_t bcd = get_spd(smb_idx, slot_idx, 93);
    spdi.fab_year = bcd - 6 * (bcd >> 4);

    bcd = get_spd(smb_idx, slot_idx, 94);
    spdi.fab_week = bcd - 6 * (bcd >> 4);

    spdi.isValid = true;

    return spdi;
}

// --------------------------
// Smbus Controller Functions
// --------------------------

static int find_smb_controller(void)
{
    int i = 0;
    unsigned long valuev, valued;

    for (smbdev = 0; smbdev < 32; smbdev++) {
        for (smbfun = 0; smbfun < 8; smbfun++) {
            valuev = pci_config_read16(0, smbdev, smbfun, 0);
            if (valuev != 0xFFFF) {
                for (i = 0; smbcontrollers[i].vendor > 0; i++) {
                    if (valuev == smbcontrollers[i].vendor) {
                        valued = pci_config_read16(0, smbdev, smbfun, 2);
                        if (valued == smbcontrollers[i].device) {
                            return i;
                        }
                    }
                }
            }
        }
    }

    return -1;
}

// ------------------
// i801 / ICH5 Access
// ------------------

static void ich5_get_smb(void)
{
    unsigned long x;

    x = pci_config_read16(0, smbdev, smbfun, 0x20);
    smbusbase = (unsigned short) x & 0xFFF0;

    // Enable I2C Bus
    uint8_t temp = pci_config_read8(0, smbdev, smbfun, 0x40);
    if((temp & 4) == 0) {
        pci_config_write8(0, smbdev, smbfun, 0x40, temp | 0x04);
    }

    // Reset SMBUS Controller
    __outb(__inb(SMBHSTSTS) & 0x1F, SMBHSTSTS);
    usleep(1000);
}

/*************************************************************************************
/ *****************************         WARNING          *****************************
/ ************************************************************************************
/   Be absolutely sure to know what you're doing before changing the function below!
/       You can easily WRITE into the SPD (especially with DDR5) and corrupt it
/                /!\  Your RAM modules will not work anymore  /!\
/ *************************************************************************************/

static uint8_t ich5_read_spd_byte(uint8_t smbus_adr, uint16_t spd_adr)
{
    smbus_adr += 0x50;

    if (dmi_memory_device->type == DMI_DDR4) {
        // Switch page if needed (DDR4)
        if (spd_adr > 0xFF && spd_page != 1) {
            __outb((0x37 << 1) | I2C_WRITE, SMBHSTADD);
            __outb(SMBHSTCNT_BYTE_DATA, SMBHSTCNT);

            ich5_process(); // return should 0x42 or 0x44
            spd_page = 1;

        } else if (spd_adr <= 0xFF && spd_page != 0) {
            __outb((0x36 << 1) | I2C_WRITE, SMBHSTADD);
            __outb(SMBHSTCNT_BYTE_DATA, SMBHSTCNT);

            ich5_process();
            spd_page = 0;
        }

        if (spd_adr > 0xFF) {
            spd_adr -= 0x100;
        }
    } else if (dmi_memory_device->type == DMI_DDR5) {
        // Switch page if needed (DDR5)
        uint8_t adr_page = spd_adr / 128;

        if (adr_page != spd_page || last_adr != smbus_adr) {
            __outb((smbus_adr << 1) | I2C_WRITE, SMBHSTADD);
            __outb(SPD5_MR11 & 0x7F, SMBHSTCMD);
            __outb(adr_page, SMBHSTDAT0);
            __outb(SMBHSTCNT_BYTE_DATA, SMBHSTCNT);

            ich5_process();

            spd_page = adr_page;
            last_adr = smbus_adr;
         }

          spd_adr -= adr_page * 128;
          spd_adr |= 0x80;
    }

    __outb((smbus_adr << 1) | I2C_READ, SMBHSTADD);
    __outb(spd_adr, SMBHSTCMD);
    __outb(SMBHSTCNT_BYTE_DATA, SMBHSTCNT);

    if (ich5_process() == 0) {
        return __inb(SMBHSTDAT0);
    } else {
        return 0xFF;
    }
}

static uint8_t ich5_process(void)
{
    uint8_t status;
    uint16_t timeout = 0;

    status = __inb(SMBHSTSTS) & 0x1F;

    if (status != 0x00) {
        __outb(status, SMBHSTSTS);
        usleep(500);
        if ((status = (0x1F & __inb(SMBHSTSTS))) != 0x00) {
            return 1;
        }
    }

    __outb(__inb(SMBHSTCNT) | SMBHSTCNT_START, SMBHSTCNT);

    do {
        usleep(500);
        status = __inb(SMBHSTSTS);
    } while ((status & 0x01) && (timeout++ < 100));

    if (timeout >= 100) {
        return 2;
    }

    if (status & 0x1C) {
        return status;
    }

    if ((__inb(SMBHSTSTS) & 0x1F) != 0x00) {
        __outb(inb(SMBHSTSTS), SMBHSTSTS);
    }

    return 0;
}

// --------------------
// AMD SMBUS Controller
// --------------------

static void fch_zen_get_smb(void)
{
    uint16_t pm_reg;

    smbusbase = 0;

    __outb(AMD_PM_INDEX + 1, AMD_INDEX_IO_PORT);
    pm_reg = __inb(AMD_DATA_IO_PORT) << 8;
    __outb(AMD_PM_INDEX, AMD_INDEX_IO_PORT);
    pm_reg |= __inb(AMD_DATA_IO_PORT);

    // Special case for AMD Cezanne (get smb address in memory)
    if (imc_type == IMC_K19_CZN && pm_reg == 0xFFFF) {
        smbusbase = ((*(const uint32_t *)(0xFED80000 + 0x300) >> 8) & 0xFF) << 8;
        return;
    }

    // Check if IO Smbus is enabled.
    if((pm_reg & 0x10) == 0){
        return;
    }

    if((pm_reg & 0xFF00) != 0) {
        smbusbase = pm_reg & 0xFF00;
    }
}
