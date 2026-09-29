#pragma once

constexpr int MALLOC_CAP_8BIT = 0;
inline unsigned heap_caps_get_largest_free_block(int) { return 100000; }
