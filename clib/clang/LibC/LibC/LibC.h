#pragma once

#include <stddef.h>

#define EXPORT __attribute__((visibility("default")))

EXPORT int LibCTest(void);
