# Contributing to Nautilus

Thank you for your interest in contributing to our project!

To build the development version of the Files app and hack on the code
see the [welcome guide](https://welcome.gnome.org/en/app/Nautilus/#getting-the-app-to-build).

Commit messages should follow the expected format [detailed here](https://handbook.gnome.org/development/commit-messages.html).

Your commit message should look something like this:

```
tag: Short explanation of the commit

Longer explanation explaining exactly what's changed and why, whether
any external or private interfaces changed, what issue were fixed (with
issue number if applicable) and so forth. Be concise but not too brief.

Closes #1234
```

## Inspector Page

For debugging performance, Files can add a page to the
[GTK Inspector](https://developer.gnome.org/documentation/tools/inspector.html)
with statistics about loading folders, searching, thumbnails, the directory
cache and extensions. It is not built by default:

```bash
meson setup -Dinspector-page=true _build
```

Enable the inspector keyboard shortcut, then press <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd>
in Files and open the "Files" tab:

```bash
gsettings set org.gtk.gtk4.Settings.Debug enable-inspector-keybinding true
```

Running with `GTK_DEBUG=interactive` does not show the page, because the
inspector is then created before Files registers it.

Statistics are only collected while the inspector is shown, so open it before
loading the folder or starting the search you want to look at. The "View" tab
follows the active tab of the most recently focused window, or the view
containing the widget picked in the "Objects" tab. The copy button puts all
statistics on the clipboard as Markdown, to paste them in an issue.

When `sysprof-capture-4` is available at build time, loading folders,
searches, thumbnails, deep counts and extension calls also show up as marks in
the "Files" group of the [Sysprof](https://apps.gnome.org/Sysprof/) timeline,
when Files is started from Sysprof, without opening the inspector:

```bash
sysprof-cli --use-trace-fd -- nautilus
```

## File Chooser

Developing for the nautilus file chooser may be complicated because of the
portal implementation.  In order to call the development version of nautilus
(rather than the system installed version) use the below commands, while making
sure that you have the development version already running.  Note that the
below commands are somewhat redundant and include default keys in order to make
updating them easier. For example, `OpenFile` defaults to `directory = false`,
but you can easily open a directory file chooser by changing `false` to `true`.

##### Open a file or directory

```bash
gdbus call --session --dest org.gnome.Nautilus.Devel --object-path /org/freedesktop/portal/desktop --method org.freedesktop.impl.portal.FileChooser.OpenFile '/org/gnome/Nautilus/Devel' 'org.gnome.Nautilus.Devel' '' 'Open a File' "{'filters': <[('All Files', [(uint32 0, '*')]), ('Images', [(uint32 0, '*.ico'), (uint32 1, 'image/png')]), ('Text', [(uint32 0, '*.txt')])]>, 'current_filter': <('All Files', [(uint32 0, '*')])>, 'choices': <[('encoding', 'Encoding', [('utf8', 'Unicode (UTF-8)'), ('latin15', 'Western')], 'latin15'), ('reencode', 'Reencode', [], 'false')]>, 'directory': <false>, 'multiple': <false>, 'modal': <true>, 'accept_label': <'Open'>, 'current_folder': <'/tmp'>}"
```

##### Save a file

```bash
gdbus call --session --dest org.gnome.Nautilus.Devel --object-path /org/freedesktop/portal/desktop --method org.freedesktop.impl.portal.FileChooser.SaveFile '/org/gnome/Nautilus/Devel' 'org.gnome.Nautilus.Devel' '' 'Save a File' "{'filters': <[('All Files', [(uint32 0, '*')]),('Images', [(uint32 0, '*.ico'), (uint32 1, 'image/png')]), ('Text', [(uint32 0, '*.txt')])]>, 'current_filter': <('All Files', [(uint32 0, '*')])>, 'choices': <[('encoding', 'Encoding', [('utf8', 'Unicode (UTF-8)'), ('latin15', 'Western')], 'latin15'), ('reencode', 'Reencode', [], 'false')]>,  'modal': <true>, 'accept_label': <'Save'>, 'current_name': <'test_file'>}"
```

##### Save multiple files

```bash
gdbus call --session --dest org.gnome.Nautilus.Devel --object-path /org/freedesktop/portal/desktop --method org.freedesktop.impl.portal.FileChooser.SaveFiles '/org/gnome/Nautilus/Devel' 'org.gnome.Nautilus.Devel' '' 'Save Files' "{'choices': <[('encoding', 'Encoding', [('utf8', 'Unicode (UTF-8)'), ('latin15', 'Western')], 'latin15'), ('reencode', 'Reencode', [], 'false')]>,  'modal': <true>, 'accept_label': <'Save'>, 'current_folder': <'/tmp'>}"
```
