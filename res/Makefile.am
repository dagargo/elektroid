if ELEKTROID_CLI_ONLY
  res_DATA = THANKS
  desktop_DATA =
  metainfo_DATA =
  svgicon_DATA =
  microbrute_DATA =
  ico_DATA =
else
  res_DATA = gui.glade gui.css THANKS
  svgicon_DATA = elektroid.svg \
                 elektroid-file-symbolic.svg \
                 elektroid-folder-symbolic.svg \
                 elektroid-keys-symbolic.svg \
                 elektroid-project-symbolic.svg \
                 elektroid-sequence-symbolic.svg \
                 elektroid-sound-symbolic.svg \
                 elektroid-symbolic.svg \
                 elektroid-wave-symbolic.svg \
                 elektroid-wavetable-symbolic.svg
  microbrute_DATA = microbrute/gui.glade

  # System dependent resources

if MINGW
    ico_DATA = elektroid.ico
else
    ico_DATA =
endif

if OPENDESKTOP
    desktop_DATA = elektroid.desktop
    metainfo_DATA = elektroid.appdata.xml
else
    desktop_DATA =
    metainfo_DATA =
endif

endif

elektron_DATA = elektron/devices.json

# Elektroid data dirs
resdir = $(datadir)/elektroid
icodir = $(datadir)/elektroid
desktopdir = $(datadir)/applications
metainfodir= $(datadir)/metainfo
svgicondir = $(datarootdir)/icons/hicolor/scalable/apps

# Connectors data dirs
elektrondir = $(resdir)/elektron
microbrutedir = $(resdir)/microbrute

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datarootdir)/icons/hicolor

install-data-hook: update-icon-cache

uninstall-hook: update-icon-cache

update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated. After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi

EXTRA_DIST = \
        $(res_DATA) \
        $(desktop_DATA) \
        $(metainfo_DATA) \
        $(svgicon_DATA) \
        $(elektron_DATA) \
        $(microbrute_DATA) \
        $(ico_DATA) \
	THANKS
