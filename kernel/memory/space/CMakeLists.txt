# cmake file for kernel memory space unit tests

add_unit_test(FILE allocations_test.c INCLUDES ${KMEM_INCLUDES} LIBS libds)
add_unit_test(FILE context_test.c INCLUDES ${KMEM_INCLUDES} LIBS libds)
target_sources(context_test PRIVATE create.c struct.c)
add_unit_test(FILE map_test.c INCLUDES ${KMEM_INCLUDES} LIBS libds)
target_sources(map_test PRIVATE mapping.c)
