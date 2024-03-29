## Makefile.am for gtk+/demos
include $(top_srcdir)/Makefile.decl

democodedir=$(datadir)/gtk-$(GTK_API_VERSION)/demo

## These should be in the order you want them to appear in the
## demo app, which means alphabetized by demo title, not filename
demos =						\
	application.c				\
	appwindow.c				\
	assistant.c				\
	builder.c				\
	button_box.c				\
	changedisplay.c				\
	clipboard.c				\
	colorsel.c				\
	combobox.c				\
	css_accordion.c                         \
	css_basics.c				\
	css_multiplebgs.c			\
	css_pixbufs.c				\
	css_shadows.c				\
	dialog.c				\
	drawingarea.c				\
	editable_cells.c			\
	entry_buffer.c				\
	entry_completion.c			\
	expander.c  				\
	hypertext.c				\
	iconview.c				\
	iconview_edit.c				\
	images.c				\
	infobar.c				\
	links.c					\
	list_store.c				\
	menus.c					\
	offscreen_window.c			\
	offscreen_window2.c			\
	overlay.c				\
	panes.c					\
	pickers.c				\
	pixbufs.c				\
	printing.c				\
	rotated_text.c				\
	search_entry.c				\
	sizegroup.c				\
	spinner.c				\
	stock_browser.c				\
	textview.c				\
	textscroll.c				\
	theming_style_classes.c			\
	theming_custom_css.c			\
	toolpalette.c				\
	transparent.c				\
	tree_store.c				\
	ui_manager.c

AM_CPPFLAGS = \
	-DDEMOCODEDIR="\"$(democodedir)\""	\
	-I$(top_srcdir) 			\
	-I$(top_builddir)/gdk			\
	-DGDK_DISABLE_DEPRECATED		\
	-DGTK_DISABLE_DEPRECATED 		\
	$(GTK_DEBUG_FLAGS)			\
	$(GTK_DEP_CFLAGS)

DEPS = \
	$(top_builddir)/gtk/libgtk-3.la

LDADDS = \
	$(top_builddir)/gtk/libgtk-3.la	\
	$(top_builddir)/gdk/libgdk-3.la	\
	$(GTK_DEP_LIBS)				\
	-lm

bin_PROGRAMS = gtk3-demo gtk3-demo-application

BUILT_SOURCES = demos.h demo_resources.c

EXTRA_DIST += 				\
	$(IMAGEFILES)			\
	demo.ui				\
	demo.gresource.xml		\
	$(RESOURCES)			\
	org.gtk.Demo.gschema.xml

gsettings_SCHEMAS = \
	org.gtk.Demo.gschema.xml

@GSETTINGS_RULES@

demos.h: @REBUILD@ $(demos) geninclude.pl
	$(AM_V_GEN) (here=`pwd` ; cd $(srcdir) && $(PERL) $$here/geninclude.pl $(demos)) > demos.h

gtk3_demo_SOURCES = 		\
	$(demos)		\
	demo-common.h		\
	demo_resources.c	\
	main.c			\
	demos.h

gtk3_demo_DEPENDENCIES = $(DEPS)
gtk3_demo_LDADD = $(LDADDS)
gtk3_demo_LDFLAGS = -export-dynamic

gtk3_demo_application_SOURCES = \
	application-standalone.c \
	demo_resources.c

gtk3_demo_application_LDADD = $(LDADDS)

demo_resources.c: demo.gresource.xml $(RESOURCES)
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate-source $(srcdir)/demo.gresource.xml

RESOURCES=	application.ui			\
		menus.ui			\
		theming.ui			\
		gtk-logo-24.png			\
		gtk-logo-48.png			\
		css_accordion.css		\
		css_basics.css			\
		css_multiplebgs.css		\
		css_pixbufs.css			\
		css_shadows.css			\
		cssview.css			\
		fancy.css			\
		reset.css

IMAGEFILES=	alphatest.png		\
		apple-red.png		\
		background.jpg		\
		floppybuddy.gif		\
		gnome-applets.png	\
		gnome-calendar.png	\
		gnome-fs-directory.png	\
		gnome-fs-regular.png	\
		gnome-foot.png		\
		gnome-gimp.png		\
		gnome-gmush.png		\
		gnome-gsame.png		\
		gnu-keys.png		\
		gtk-logo-rgb.gif

democode_DATA = \
	$(demos)		\
	$(IMAGEFILES)		\
	$(RESOURCES)		\
	demo.ui

DISTCLEANFILES = demos.h

-include $(top_srcdir)/git.mk
