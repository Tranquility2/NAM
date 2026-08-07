# Acceptance sweeps: the seeded prototype gate, the determinism contract, the
# handcrafted Phase 0 level walked end to end, and the journal/command budgets
# measured over whole played expeditions. They are property-style — many seeds,
# many steps — so they cost more than the rest of the suite combined and are
# labelled `slow` to let CI run the fast tiers on their own.
#
# The budget sweep folds real core events into the console journal, so this is
# the one acceptance target that links the console library. The dependency is one
# way and stays that way: nothing in nam_core knows the journal exists.
nam_add_test_executable(nam_acceptance_tests
    LINK nam_core nam_console_lib
    LABELS "acceptance;slow"
    SOURCES
        prototype_gate.cpp
        determinism.cpp
        phase0_level.cpp
        expedition_budgets.cpp)
