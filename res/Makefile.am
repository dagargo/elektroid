resdir = $(datadir)/elektroid/res
res_DATA = gui.glade gui.css

desktopdir = $(datadir)/applications
desktop_DATA = elektroid.desktop

svgicondir = $(datarootdir)/icons/hicolor/scalable/apps
svgicon_DATA = elektroid.svg

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datarootdir)/icons/hicolor

install-data-hook: update-icon-cache

uninstall-hook: update-icon-cache

update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi
EXTRA_DIST = \
        $(icon_DATA) \
        $(svgicon_DATA)
