# Optional SEA-RAFT alignment package

SEA-RAFT was developed by its upstream authors, not by Stack. Code is from
princeton-vl/SEA-RAFT, commit 9137517ba24e628442aec097d3afe71d03503b75.
Retain `LICENSE-SEA-RAFT.txt` with this package. The checkpoint is
MemorySlices/Tartan-C-T-TSKH-spring540x960-M, revision
eb97ef34ba5d856c3fa2cdcd073150c057ac8b69; retain its `MODEL-CARD.md` and the
upstream license. Artifact URLs and checksums are in `artifacts.json` in the
source tooling.

Stack's adapter removes the online model-loading mixin, disables initialization
that is overwritten by the complete checkpoint, and defers an unused training
helper's scipy import. It keeps the network and strict checkpoint loading.
The model proposes motion; Stack verifies it against RAW captures and never
uses generated image pixels.

The Python interpreter and PyTorch, NumPy and safetensors dependencies are
separate components with their own licenses. A distributable optional package
must retain those components' notices and include its own compatible runtime.
The development setup can refer to an explicitly selected existing interpreter;
that configuration is not a self-contained public installer.
