EXTRA_DIST = touchpad-tap-state-machine.svg touchpad-softbutton-state-machine.svg

if BUILD_DOCS

noinst_DATA = html/index.html

header_files = \
	$(top_srcdir)/src/libinput.h \
	$(top_srcdir)/README.txt \
	$(srcdir)/absolute-axes.dox \
	$(srcdir)/clickpad-softbuttons.dox \
	$(srcdir)/device-configuration-via-udev.dox \
	$(srcdir)/normalization-of-relative-motion.dox \
	$(srcdir)/palm-detection.dox \
	$(srcdir)/scrolling.dox \
	$(srcdir)/seats.dox \
	$(srcdir)/t440-support.dox \
	$(srcdir)/tapping.dox

diagram_files = \
	$(srcdir)/dot/seats-sketch.gv \
	$(srcdir)/dot/seats-sketch-libinput.gv \
	$(srcdir)/svg/button-scrolling.svg \
	$(srcdir)/svg/edge-scrolling.svg \
	$(srcdir)/svg/palm-detection.svg \
	$(srcdir)/svg/twofinger-scrolling.svg

html/index.html: libinput.doxygen $(header_files) $(diagram_files)
	$(AM_V_GEN)(cat $<; \
          echo "INPUT = $(header_files)"; \
          echo "DOTFILE_DIRS = $(srcdir)/dot"; \
          ) | $(DOXYGEN) -

clean-local:
	$(AM_V_at)rm -rf html

doc_src= $(shell find html -type f -printf "html/%P\n" 2>/dev/null)
EXTRA_DIST += $(builddir)/html/index.html $(doc_src)

endif

# make sure doc was built before running dist
dist-hook:
	@test -f $(distdir)/html/index.html || (\
		echo "******************************************************" && \
		echo "Couldn't find documentation files, refusing make dist." && \
		echo "Install doxygen to build documentation for tarball." && \
		echo "******************************************************" && \
		test )
