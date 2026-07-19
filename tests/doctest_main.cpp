// The single translation unit that emits doctest's implementation and a default
// main(). Every test executable links this once; the test source files include
// only the doctest header and register cases via static initialisation.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
