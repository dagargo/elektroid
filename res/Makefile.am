res_DATA = libraries.html \
	THANKS \
	volca_sample_sdk-COPYING

if ELEKTROID_CLI_ONLY
  desktop_DATA =
  metainfo_DATA =
  svgicon_DATA =
  microbrute_DATA =
  ico_DATA =
else
  res_DATA += elektroid.ui elektroid.css
  svgicon_DATA = io.github.dagargo.Elektroid.svg \
                 io.github.dagargo.Elektroid-symbolic.svg \
                 elektroid-file-symbolic.svg \
                 elektroid-folder-symbolic.svg \
                 elektroid-keys-symbolic.svg \
                 elektroid-project-symbolic.svg \
                 elektroid-sequence-symbolic.svg \
                 elektroid-sound-symbolic.svg \
                 elektroid-wave-symbolic.svg \
                 elektroid-wavetable-symbolic.svg
  microbrute_DATA = microbrute/microbrute.ui

  # System dependent resources

if MINGW
    ico_DATA = elektroid.ico
else
    ico_DATA =
endif

if OPENDESKTOP
    desktop_DATA = io.github.dagargo.Elektroid.desktop
    metainfo_DATA = io.github.dagargo.Elektroid.appdata.xml
else
    desktop_DATA =
    metainfo_DATA =
endif

endif

elektron_DATA = elektron/devices.json
volca_sample_2_DATA = volca_sample_2/init_pattern.vlcsplpattb

# Elektroid data dirs
resdir = $(datadir)/elektroid
icodir = $(datadir)/elektroid
desktopdir = $(datadir)/applications
metainfodir= $(datadir)/metainfo
svgicondir = $(datarootdir)/icons/hicolor/scalable/apps

# Connectors data dirs
elektrondir = $(resdir)/elektron
microbrutedir = $(resdir)/microbrute
volca_sample_2dir = $(resdir)/volca_sample_2

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
        $(volca_sample_2_DATA) \
        $(ico_DATA) \
	THANKS
