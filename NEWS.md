Changelog

# v2.0.0 - December 6, 2019
- Drop Python 2.7 support – **this version supports Python 3.6+ ONLY**
- [modify CLI argument parsing](https://github.com/adobe-type-tools/psautohint/issues/176) to allow the same options as AFDKO autohint and [improve `-o` option](https://github.com/adobe-type-tools/psautohint/issues/129)
- use `fonttools` 4.0.2
- fix some mysterious memory issues
- [clean up and improve the formatting of `psstemhist` reports](https://github.com/adobe-type-tools/psautohint/issues/153)
- add [LGTM.com/Semmle](https://lgtm.com/projects/g/adobe-type-tools/psautohint/?mode=tree) config. `psautohint` Pull Requests are now automatically analyzed for a variety of problems (security, coding style, etc.)
- [*tons* of fixes to both Python and C++ code based on ongoing LGTM.com reports](https://lgtm.com/projects/g/adobe-type-tools/psautohint/history/)
- refactoring to allow sharing paths between hinting source fonts for MM fonts, and [hinting a CFF2 variable font](https://github.com/adobe-type-tools/psautohint/issues/105)
- removed unused code
- dropped support for using `autohintexe` (it is still _built_, just not used by `psautohint`...it will eventually be removed from builds also)
- fix [bug with compatible hinting (-r option)](https://github.com/adobe-type-tools/psautohint/issues/189)
- remove Codacy analysis and related badge
- implemented workaround for [a nagging crash with -O3 optimization under Linux](https://github.com/adobe-type-tools/psautohint/issues/103)
