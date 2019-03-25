#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>
#include <inttypes.h>

#include "par_malloc.h"

int main(int _ac, const char* _av[]) {
    for (int ii = 0; ii < 750; ii++) {
        opt_malloc(2048);
    }

  return 0;
}
