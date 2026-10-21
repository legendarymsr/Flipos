# flipos top-level build. Buildroot lives in ./buildroot (git submodule, pinned);
# all our customization is in ./br2-external. Output, downloads, and ccache live
# OUTSIDE the Buildroot tree so a thermal kill / crash never loses work — a rerun
# of `make image` resumes.

BR            := buildroot
EXT           := $(CURDIR)/br2-external
O             := $(CURDIR)/out
export BR2_DL_DIR := $(CURDIR)/dl
export BR2_CCACHE_DIR := $(CURDIR)/ccache
DEFCONFIG     := flipos_qemu_aarch64_defconfig
# Reproducibility: pin the epoch to the pinned Buildroot tag's commit date.
export SOURCE_DATE_EPOCH ?= 1704067200   # 2024-01-01; re-pinned in Phase 1

.DEFAULT_GOAL := help

.PHONY: help
help: ## show targets
	@grep -hE '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | awk 'BEGIN{FS=":.*?## "}{printf "  \033[36m%-16s\033[0m %s\n",$$1,$$2}'

$(BR)/Makefile:
	@echo "Buildroot submodule missing. Run: git submodule update --init --depth 1 buildroot"; exit 1

.PHONY: defconfig
defconfig: $(BR)/Makefile ## apply the flipos defconfig into out/
	$(MAKE) -C $(BR) BR2_EXTERNAL=$(EXT) O=$(O) $(DEFCONFIG)

.PHONY: menuconfig
menuconfig: defconfig ## tweak the config interactively
	$(MAKE) -C $(BR) O=$(O) menuconfig

.PHONY: image
image: defconfig ## build the kernel + EROFS root into out/images/ (resumable)
	$(MAKE) -C $(BR) O=$(O)

.PHONY: run
run: ## boot the built image under QEMU (auto KVM/TCG + display)
	scripts/run.sh $(RUN_ARGS)

.PHONY: test
test: ## headless boot/UI/sandbox tests (no display)
	scripts/test/run-tests.sh

.PHONY: verify-repro
verify-repro: ## build twice from clean and diff the images
	scripts/verify-repro.sh

.PHONY: clean
clean: ## remove build output (keeps dl/ and ccache/)
	rm -rf $(O)

.PHONY: distclean
distclean: ## remove output, downloads, and ccache
	rm -rf $(O) $(CURDIR)/dl $(CURDIR)/ccache

.PHONY: release
release: ## package the flashable KernelSU module zip (out/flipos-<ver>.zip)
	scripts/mkrelease.sh

.PHONY: qemu-android
qemu-android: ## cross-build a native Android qemu (NDK) into out/android-qemu
	scripts/build-qemu-android.sh
