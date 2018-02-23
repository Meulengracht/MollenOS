#include <stdio.h>
#include <math.h>

int inf_main();
int inf_main2();
int inf_main3();
int inf_main4();

int inf_main() {
        printf("+inf:\n");
        float fx = (float)INFINITY; unsigned int *fxi = (unsigned int*)&fx;
        double dx = (double)INFINITY; long unsigned long int *dxi = (long unsigned long int*)&dx;
        long double ldx = (long double)INFINITY; long unsigned long int *ldxi1 = (long unsigned long int*)&ldx; long unsigned long int *ldxi2 = &(ldxi1[1]);
        printf("\t\tf d ld\n");
        printf("correct:\t%x %x %x\n", isinf(fx), isinf(dx), isinf(ldx));
        printf("as floats:\t%x %x %x\n", isinf(*(float*)fxi), isinf(*(float*)dxi), isinf(*(float*)ldxi1));
        printf("as double:\t%x %x %x\n", isinf(*(double*)fxi), isinf(*(double*)dxi), isinf(*(double*)ldxi1));
        printf("as long double:\t%x %x %x\n", isinf(*(long double*)fxi), isinf(*(long double*)dxi), isinf(*(long double*)ldxi1));
        printf("sizes ?4 8 12?:\t%d %d %d\n", (int)sizeof(fx), (int)sizeof(dx), (int)sizeof(ldx));
        printf("sizes:\t%d %d %d\n", (int)sizeof(*fxi), (int)sizeof(*dxi), (int)sizeof(*ldxi1)*2);
        printf("bit repr:\n  f: %x\n  d: %llx\n ld: %llx%llx\n", *fxi, *dxi, (0xFFFF)&*ldxi2, *ldxi1);
        printf("\n");
        inf_main2();
        return 0;
}

int inf_main2() {
        printf("-inf:\n");
        float fx = (float)-INFINITY; unsigned int *fxi = (unsigned int*)&fx;
        double dx = (double)-INFINITY; long unsigned long int *dxi = (long unsigned long int*)&dx;
        long double ldx = (long double)-INFINITY; long unsigned long int *ldxi1 = (long unsigned long int*)&ldx; long unsigned long int *ldxi2 = &(ldxi1[1]);
        printf("\t\tf d ld\n");
        printf("correct:\t%x %x %x\n", isinf(fx), isinf(dx), isinf(ldx));
        printf("as floats:\t%x %x %x\n", isinf(*(float*)fxi), isinf(*(float*)dxi), isinf(*(float*)ldxi1));
        printf("as double:\t%x %x %x\n", isinf(*(double*)fxi), isinf(*(double*)dxi), isinf(*(double*)ldxi1));
        printf("as long double:\t%x %x %x\n", isinf(*(long double*)fxi), isinf(*(long double*)dxi), isinf(*(long double*)ldxi1));
        printf("sizes ?4 8 12?:\t%d %d %d\n", (int)sizeof(fx), (int)sizeof(dx), (int)sizeof(ldx));
        printf("bit repr:\n  f: %x\n  d: %llx\n ld: %llx%llx\n", *fxi, *dxi, (0xFFFF)&*ldxi2, *ldxi1);
        printf("\n");
		inf_main3();
        return 0;
}

int inf_main3() {
        printf("+NaN:\n");
        float fx = (float)NAN; unsigned int *fxi = (unsigned int*)&fx;
        double dx = (double)NAN; long unsigned long int *dxi = (long unsigned long int*)&dx;
        long double ldx = (long double)NAN; long unsigned long int *ldxi1 = (long unsigned long int*)&ldx; long unsigned long int *ldxi2 = &(ldxi1[1]);
        printf("\t\tf d ld\n");
        printf("correct:\t%x %x %x\n", isnan(fx), isnan(dx), isnan(ldx));
        printf("as floats:\t%x %x %x\n", isnan(*(float*)fxi), isnan(*(float*)dxi), isnan(*(float*)ldxi1));
        printf("as double:\t%x %x %x\n", isnan(*(double*)fxi), isnan(*(double*)dxi), isnan(*(double*)ldxi1));
        printf("as long double:\t%x %x %x\n", isnan(*(long double*)fxi), isnan(*(long double*)dxi), isnan(*(long double*)ldxi1));
        printf("sizes ?4 8 12?:\t%d %d %d\n", (int)sizeof(fx), (int)sizeof(dx), (int)sizeof(ldx));
        printf("sizes:\t%d %d %d\n", (int)sizeof(*fxi), (int)sizeof(*dxi), (int)sizeof(*ldxi1)*2);
        printf("bit repr:\n  f: %x\n  d: %llx\n ld: %llx%llx\n", *fxi, *dxi, (0xFFFF)&*ldxi2, *ldxi1);
        printf("\n");
        inf_main4();
        return 0;
}

int inf_main4() {
        printf("-NaN:\n");
        float fx = (float)-NAN; unsigned int *fxi = (unsigned int*)&fx;
        double dx = (double)-NAN; long unsigned long int *dxi = (long unsigned long int*)&dx;
        long double ldx = (long double)-NAN; long unsigned long int *ldxi1 = (long unsigned long int*)&ldx; long unsigned long int *ldxi2 = &(ldxi1[1]);
        printf("\t\tf d ld\n");
        printf("correct:\t%x %x %x\n", isnan(fx), isnan(dx), isnan(ldx));
        printf("as floats:\t%x %x %x\n", isnan(*(float*)fxi), isnan(*(float*)dxi), isnan(*(float*)ldxi1));
        printf("as double:\t%x %x %x\n", isnan(*(double*)fxi), isnan(*(double*)dxi), isnan(*(double*)ldxi1));
        printf("as long double:\t%x %x %x\n", isnan(*(long double*)fxi), isnan(*(long double*)dxi), isnan(*(long double*)ldxi1));
        printf("sizes ?4 8 12?:\t%d %d %d\n", (int)sizeof(fx), (int)sizeof(dx), (int)sizeof(ldx));
        printf("bit repr:\n  f: %x\n  d: %llx\n ld: %llx%llx\n", *fxi, *dxi, (0xFFFF)&*ldxi2, *ldxi1);
        printf("\n");
        return 0;
}
