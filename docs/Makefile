# The project README.md is generated from the documentation pages by executing `make` on this directory.

README=../README.md

$(README): index.md installation.md packaging.md cli.md api.md tests.md
	echo '# Elektroid' > $(README)
	echo >> $(README)
	echo '[//]: # (Do not modify this file manually.)' >> $(README)
	echo '[//]: # (This file is generated from the docs directory by executing `make`.)' >> $(README)
	tail -n +4 index.md | sed "s^images/screenshot.png^docs/images/screenshot.png^" >> $(README)
	tail -n +7 installation.md >> $(README)
	tail -n +7 packaging.md >> $(README)
	tail -n +7 cli.md >> $(README)
	tail -n +7 api.md | sed "s^https://github.com/dagargo/elektroid/tree/master/^^g" >> $(README)
	tail -n +7 tests.md >> $(README)
