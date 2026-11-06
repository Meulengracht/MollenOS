# cmake file for kernel memory shared memory unit tests

add_unit_test(FILE shm_test.c INCLUDES ${KMEM_INCLUDES} LIBS libds)
