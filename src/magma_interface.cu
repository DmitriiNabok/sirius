// This file must be compiled with nvcc

#include <stdio.h>
#include <assert.h>
#include <cuda.h>
#include <magma_z.h>
#include "gpu_interface.h"

extern "C" magma_int_t magma_zbulge_get_lq2(magma_int_t n);

extern "C" magma_int_t magma_zhegvdx_2stage(magma_int_t itype, char jobz, char range, char uplo, magma_int_t n,
                                            cuDoubleComplex *a, magma_int_t lda, cuDoubleComplex *b, magma_int_t ldb,
                                            double vl, double vu, magma_int_t il, magma_int_t iu, magma_int_t *m, 
                                            double *w, cuDoubleComplex *work, magma_int_t lwork, double *rwork, 
                                            magma_int_t lrwork, magma_int_t *iwork, magma_int_t liwork, 
                                            magma_int_t *info);

extern "C" void magma_zhegvdx_2stage_wrapper(int32_t matrix_size, int32_t nv, void* a, int32_t lda, void* b, 
                                             int32_t ldb, double* eval)
{
    int m;
    int info;

    int lwork = magma_zbulge_get_lq2(matrix_size) + 2 * matrix_size + matrix_size * matrix_size;
    int lrwork = 1 + 5 * matrix_size + 2 * matrix_size * matrix_size;
    int liwork = 3 + 5 * matrix_size;
            
    cuDoubleComplex* h_work;
    cuda_malloc_host((void**)&h_work, lwork * sizeof(cuDoubleComplex));

    double* rwork;
    cuda_malloc_host((void**)&rwork, lrwork * sizeof(double));
    
    magma_int_t *iwork;
    if ((iwork = (magma_int_t*)malloc(liwork * sizeof(magma_int_t))) == NULL)
    {
        printf("malloc failed\n");
        exit(-1);
    }
    
    double* w;
    if ((w = (double*)malloc(matrix_size * sizeof(double))) == NULL)
    {
        printf("mallof failed\n");
        exit(-1);
    }

    magma_zhegvdx_2stage(1, 'V', 'I', 'L', matrix_size, (cuDoubleComplex*)a, lda, (cuDoubleComplex*)b, ldb, 0.0, 0.0, 
                         1, nv, &m, w, h_work, lwork, rwork, lrwork, iwork, liwork, &info);

    memcpy(eval, &w[0], nv * sizeof(double));
    
    cuda_free_host((void**)&h_work);
    cuda_free_host((void**)&rwork);
    free(iwork);
    free(w);

    if (info)
    {
        printf("magma_zhegvdx_2stage returned : %i\n", info);
        exit(-1);
    }    

    if (m != nv)
    {
        printf("Not all eigen-values are found.\n");
        exit(-1);
    }
}


