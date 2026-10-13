/* Persistent data in the external SPI NOR flash (W25Q16), same layout as the original
 * (docs/spec/hardware.md §6.1, application.md §6.1, §10). Nothing here touches internal flash. */
#pragma once

void storage_init(void);

void names_load(void);                                /* 0x0800E29C: 0x5000 -> g_names, 0xFF bytes read as 0 */
void names_save(void);                                /* 0x08011380: whole table, slots 0 and 31 zeroed */
void name_set(unsigned slot, const char *text, unsigned len);   /* 0x080124D8, <= 60 chars */

void program_load(unsigned slot, unsigned ch);        /* 0x0800E2E4: slot 1..30 -> g_ch[ch] prog + params */
void program_save(unsigned slot, unsigned ch);        /* 0x080113EC: g_ch[ch] -> slot (erase + write) */
void program_erase(unsigned slot);                    /* ':nNN=,' : erase the slot's sector only */
void storage_erase_all(void);                         /* w96: slots 1..30 and the name table */

void cal_load(void);                                  /* 0x0800E264: accepted only if word[31] == 31 */
void cal_save(int backup);                            /* 0x08011310: write if different; backup -> also 0x75000 */
void cal_restore_backup(void);                        /* 0x08011288: 0x75000 -> RAM, rewrite 0x74000 if different */
