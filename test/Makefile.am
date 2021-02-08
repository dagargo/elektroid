TESTS=full_test.sh

EXTRA_DIST = \
        $(TESTS) \
        res/square.wav

AM_TESTS_ENVIRONMENT = \
	ecli='$(abs_top_builddir)'/src/elektroid-cli; \
	export ecli;
