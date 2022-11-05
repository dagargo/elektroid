TESTS = test.sh

EXTRA_DIST = \
	$(TESTS) \
	res/square.wav \
	res/square_loop.wav \
	res/SOUND.dtdata \
	res/devices.json \
	res/sequence.mbseq \
	res/sequence_back.mbseq

AM_TESTS_ENVIRONMENT = \
	ecli='$(abs_top_builddir)/src/elektroid-cli'; \
	export ecli;
