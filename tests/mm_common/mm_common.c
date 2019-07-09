mm_t results_matrix[side][side];

void matrix_multiply(mm_t f_matrix[][side], mm_t s_matrix[][side], mm_t r_matrix[][side]) {
	int i = 0;
	int j = 0;
	int k = 0;
	unsigned long sum = 0;

	//MM
	for ( i = 0 ; i < side ; i++ ) {
		for ( j = 0 ; j < side ; j++ ) {
			for ( k = 0 ; k < side ; k++ ) {
				sum = sum + f_matrix[i][k]*s_matrix[k][j];
			}

			r_matrix[i][j] = sum;
			sum = 0;
		}
	}
}

__attribute__((noinline))
int checkGolden() {
	unsigned int xor = 0;
	unsigned int i,j;

	for(i=0; i<side; i++)
		for (j = 0; j < side; j++)
			xor ^= results_matrix[i][j];

	return (xor != xor_golden);
}

void mm_run_test() {
	matrix_multiply(first_matrix, second_matrix, results_matrix);	
}