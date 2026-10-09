# Standalone Fedora package; the SANE sources keep their own Automake build.
RPM_VERSION ?= 0.1.0
RPM_RELEASE ?= 1
RPM_TOPDIR ?= $(CURDIR)/build/rpm
PYTHON ?= python3

.PHONY: rpm srpm rpm-source
rpm srpm rpm-source:
	$(PYTHON) tools/build-rpm.py $@ --version "$(RPM_VERSION)" \
		--release "$(RPM_RELEASE)" --topdir "$(RPM_TOPDIR)"
