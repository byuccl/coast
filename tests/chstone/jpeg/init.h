/*
+--------------------------------------------------------------------------+
| CHStone : a suite of benchmark programs for C-based High-Level Synthesis |
| ======================================================================== |
|                                                                          |
| * Collected and Modified : Y. Hara, H. Tomiyama, S. Honda,               |
|                            H. Takada and K. Ishii                        |
|                            Nagoya University, Japan                      |
|                                                                          |
| * Remark :                                                               |
|    1. This source code is modified to unify the formats of the benchmark |
|       programs in CHStone.                                               |
|    2. Test vectors are added for CHStone.                                |
|    3. If "main_result" is 0 at the end of the program, the program is    |
|       correctly executed.                                                |
|    4. Please follow the copyright of each benchmark program.             |
+--------------------------------------------------------------------------+
*/
/*
 * Copyright (C) 2008
 * Y. Hara, H. Tomiyama, S. Honda, H. Takada and K. Ishii
 * Nagoya University, Japan
 * All rights reserved.
 *
 * Disclaimer of Warranty
 *
 * These software programs are available to the user without any license fee or
 * royalty on an "as is" basis. The authors disclaims  any and all warranties,
 * whether express, implied, or statuary, including any implied warranties or
 * merchantability or of fitness for a particular purpose. In no event shall the
 * copyright-holder be liable for any incidental, punitive, or consequential
 *damages
 * of any kind whatsoever arising from the use of these programs. This
 *disclaimer
 * of warranty extends to the user of these programs and user's customers,
 *employees,
 * agents, transferees, successors, and assigns.
 *
 */

#include "global.h"
#include "decode.h"

int main_result;
/*
 * Output Buffer
 */
unsigned char *CurHuffReadBuf;
int OutData_image_width;
int OutData_image_height;
int OutData_comp_vpos[RGB_NUM];
int OutData_comp_hpos[RGB_NUM];
unsigned char OutData_comp_buf[RGB_NUM][BMP_OUT_SIZE];
int out_width;
int out_length;

#define JPEGSIZE 5207

const unsigned char hana_jpg[JPEGSIZE];

const unsigned char hana_bmp[RGB_NUM][BMP_OUT_SIZE];
