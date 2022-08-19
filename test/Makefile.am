TESTS = elektron_sample_fs_tests.sh elektron_data_fs_tests.sh sds_sample_fs_tests.sh

EXTRA_DIST = \
	$(TESTS) \
	res/square.wav \
	res/square_loop.wav \
	res/SOUND.dtdata \
	res/devices.json

AM_TESTS_ENVIRONMENT = \
	ecli='$(abs_top_builddir)/src/elektroid-cli'; \
	export ecli;
