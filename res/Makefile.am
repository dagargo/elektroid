if CLI_ONLY
res_DATA = devices.json
desktop_DATA =
metainfo_DATA =
svgicon_DATA =
else
res_DATA = gui.glade gui.css devices.json
desktop_DATA = elektroid.desktop
metainfo_DATA = elektroid.appdata.xml
svgicon_DATA = elektroid.svg elektroid-symbolic.svg elektroid-wave-symbolic.svg elektroid-data-symbolic.svg elektroid-project-symbolic.svg elektroid-sound-symbolic.svg
endif

resdir = $(datadir)/elektroid
desktopdir = $(datadir)/applications
metainfodir= $(datadir)/metainfo
svgicondir = $(datarootdir)/icons/hicolor/scalable/apps

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
        $(res_DATA) \
        $(desktop_DATA) \
        $(metainfo_DATA) \
        $(svgicon_DATA)
