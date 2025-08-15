# The project README.md is generated from the documentation pages by executing `make` on this directory.

README=../README.md

$(README): index.md license.md installation.md packaging.md cli.md api.md tests.md
	echo '# Elektroid' > $(README)
	echo >> $(README)
	echo '[//]: # (Do not modify this file manually.)' >> $(README)
	echo '[//]: # (This file is generated from the docs directory by executing `make clean; make`.)' >> $(README)
	tail -n +4 index.md | sed "s^images/screenshot.png^docs/images/screenshot.png^" >> $(README)
	tail -n +7 license.md >> $(README)
	tail -n +7 installation.md >> $(README)
	tail -n +7 packaging.md >> $(README)
	tail -n +7 cli.md >> $(README)
	tail -n +7 api.md | sed "s^https://github.com/dagargo/elektroid/tree/master/^^g" >> $(README)
	tail -n +7 tests.md >> $(README)

license.md: license.md.in
	cat license.md.in > license.md
	echo '' >> license.md
	while read -r line; do echo "* $$line" >> license.md; done < libraries.md
	cat libraries.md | sed -r 's/\[([^\]*)\]\(([^\)]*)\)/<a href="\2">\1<\/a>/' > ../res/libraries.html

clean:
	rm -f $(README) license.md ../res/libraries.html
