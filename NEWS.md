Changelog

# v2.3.1 - July 28, 2021
- Updated for fontTools rounding changes (thanks, @madig!)
  ([#301](https://github.com/adobe-type-tools/psautohint/pull/301))
- Updated dependencies
# v2.3.0 - February 22, 2021
- Fixed a major performance issue when hinting VFs (thanks, @madig!)
  ([#289](https://github.com/adobe-type-tools/psautohint/pull/289),
   [#221](https://github.com/adobe-type-tools/psautohint/issues/221))
- Updated dependencies
  ([#290](https://github.com/adobe-type-tools/psautohint/pull/290),
   [#291](https://github.com/adobe-type-tools/psautohint/pull/291),
   [#292](https://github.com/adobe-type-tools/psautohint/pull/292))
# v2.2.0 - December 15, 2020
- Removed unused third-party CI configs and related code
- Updated dependencies
- Removed deprecated CLI option flags that were temporarily in place for the autohint -> psautohint transition
- Removed `autohintexe` from Python wheels. It can still be built using `python setup.py build_exe`.
- Cleaned up Python code

# v2.1.2 - November 6, 2020
- Move remaining CI workflows (building wheels, deploying) to GitHub Actions including building of Python 3.9 wheels for supported platforms.
- Updated dependencies
- Fixed some [minor formatting issues](https://github.com/adobe-type-tools/psautohint/pull/272)

# v2.1.1 - September 24, 2020
- Mute ['unhinted <glyphname>' messages from tx](https://github.com/adobe-type-tools/psautohint/issues/231)
- Updated dependencies
- Generate coverage reports from GitHub Actions
- Fix [NULL pointer access when processing taito glyph](https://github.com/adobe-type-tools/psautohint/pull/263)
- Fix [dump-font.py utility](https://github.com/adobe-type-tools/psautohint/commit/e04a11844738584bb7a666fbb69ffb840b2d19ef) (thanks @khaledhosny!)

# v2.0.1 - March 25, 2020
- Fixed [a recursion error with `-x` option](https://github.com/adobe-type-tools/psautohint/issues/223) (thanks @kontur!)
- Fixed [an error and other problems with `--print-dflt-fddict`](https://github.com/adobe-type-tools/psautohint/issues/222) (thanks again @kontur!)
- Changed [logging level of "Conflicts with current hints" message to DEBUG](https://github.com/adobe-type-tools/psautohint/pull/235/commits/69bab0df4eac8c4a88d9ac4dce94c2d6c61aba99) instead of ERROR (thanks _again_ @kontur!)
- Added [Python 3.8 wheels](https://github.com/adobe-type-tools/psautohint/pull/242) to the distribution set (thanks @miguelsousa!)

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
