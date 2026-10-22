# Ymir SDL3 translations

Ymir uses GNU gettext. `sv.po` is the Swedish catalog and builds to
`locale/sv/LC_MESSAGES/ymir-sdl3.mo` next to the executable.

To add a language, create its catalog from the template and add one explicit
line to `apps/ymir-sdl3/CMakeLists.txt`:

```sh
msginit --locale=pt_BR --input=ymir-sdl3.pot --output-file=pt_BR.po
msgfmt --check --check-format --output-file=/dev/null pt_BR.po
```

```cmake
ymir_sdl3_add_locale(pt_BR)
```

The explicit CMake entry keeps shipped languages reviewable while the helper
handles compilation, output paths and installation beside the executable.

When a source string changes, regenerate the template from the sources listed
in `apps/ymir-sdl3/CMakeLists.txt` and merge it into each catalog:

```sh
cmake --build <build-directory> --target ymir-sdl3-pot
msgmerge --update sv.po ymir-sdl3.pot
msgfmt --check --check-format --output-file=/dev/null sv.po
```

Use `_()` for a translatable string. Keep ImGui ID suffixes such as `##vdp1`
outside the translated text and use `ImGui::PushID()` when two visible strings
need separate widget identifiers.
