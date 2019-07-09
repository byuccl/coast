/*
Not initially part of the test suite, added to improve code readability
*/

#ifndef __MOTION_H
#define __MOTION_H

unsigned int Get_Bits1();
unsigned int Get_Bits(int N);
unsigned int Show_Bits(int N);
void Flush_Buffer(int N);

void motion_vector(int* PMV, int* dmvector, int h_r_size, int v_r_size, int dmv,
    int mvscale, int full_pel_vector);
void motion_vectors(int PMV[2][2][2], int dmvector[2],
                    int motion_vertical_field_select[2][2], int s,
                    int motion_vector_count, int mv_format, int h_r_size, int v_r_size,
                    int dmv, int mvscale);

int Get_motion_code();
int Get_dmvector();

extern const unsigned char inRdbfr[];

#endif  /* __MOTION_H */
