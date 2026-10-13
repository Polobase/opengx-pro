# The UI engine features of src/nx/nx_config.h that the nx image never uses, switched off for it
# (the engine gallery and the host tests keep them). One list for the two builds that must agree:
# the image (CFLAGS_nx in the Makefile) and the host renderer of its screens (tests/host/Makefile,
# gallery-dev), whose shots must match the gallery baselines.
NX_ENG_OFF := -DNX_ENG_VGRAD=0 -DNX_ENG_FADE=0 -DNX_SCOPE_GATE=0 -DNX_SCOPE_CUSTOM=0 -DNX_SCOPE_RATIO=0 \
              -DNX_SCOPE_LABELS=0
