# Castalia build entry points (Bible §17.2). Run from the repo root:
#   make -C build iso EDITION=classic64
#   make -C build dry-run
#   make -C build deb repo        # the distributable .deb + apt overlay repo
#   make -C build test-all        # every local QA tier (tests/run.sh full)
EDITION ?= classic64
REPO    := $(abspath ..)

.PHONY: iso dry-run themes sounds bootbg shell deb repo test test-all e2e

iso:
	sh $(REPO)/build/mkiso.sh --edition $(EDITION)

dry-run:
	sh $(REPO)/build/mkiso.sh --edition $(EDITION) --dry-run

themes:
	cd $(REPO) && PYTHONPATH=tools python3 tools/theme_export.py

sounds:
	cd $(REPO) && PYTHONPATH=tools python3 tools/sound_gen.py --board

bootbg:
	cd $(REPO) && PYTHONPATH=tools python3 tools/bootbg_gen.py

shell:
	cmake -S $(REPO)/shell -B $(REPO)/build/out/shell-build -DCMAKE_BUILD_TYPE=Release
	cmake --build $(REPO)/build/out/shell-build -j4

deb:
	sh $(REPO)/packages/mkdeb.sh

repo: deb
	sh $(REPO)/build/mkrepo.sh

test:
	cd $(REPO) && PYTHONPATH=tools python3 -m unittest discover -s tools/tests

test-all:
	cd $(REPO) && sh tests/run.sh full

e2e:
	cd $(REPO) && sh tests/run.sh e2e
