TESTS = sample_fs_tests.sh data_fs_tests.sh

EXTRA_DIST = \
	$(TESTS) \
	res/square.wav \
	res/devices.json

AM_TESTS_ENVIRONMENT = \
	ecli='$(abs_top_builddir)/src/elektroid-cli -f $(srcdir)/res/devices.json'; \
	export ecli;
