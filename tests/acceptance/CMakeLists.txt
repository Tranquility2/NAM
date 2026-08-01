# Acceptance sweeps: the seeded prototype gate, the determinism contract, and the
# handcrafted Phase 0 level walked end to end. They are property-style — many
# seeds, many steps — so they cost more than the rest of the suite combined and
# are labelled `slow` to let CI run the fast tiers on their own.
nam_add_test_executable(nam_acceptance_tests
    LINK nam_core
    LABELS "acceptance;slow"
    SOURCES
        prototype_gate.cpp
        determinism.cpp
        phase0_level.cpp)
