# Welcome to the COAST Repository

Welcome to the repository for COAST (COmpiler-Assisted Software fault Tolerance), BYU's tool for automated software mitigation! To get started, please refer to our [documentation pages](https://coast-compiler.readthedocs.io/en/latest/).


## Cloning

```
git clone --recursive -j2 git@github.com:byuccl/coast-private.git
```

This makes sure that the LLVM submodule repository is also cloned.  If you forget this, go to `coast/llvm-project` and execute

```
git submodule init
git submodule update
```
