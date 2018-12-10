The Standard Bezier format
============================

The Standard Bezier format is an plain text format consisting of the following
operators:

| Operator  | Description                                                      |
|-----------|------------------------------------------------------------------|
| `sc`      | start of glyph outline                                           |
| `rmt`     | relative move (same as Type 1 `rmoveto` operator)                |
| `dt`      | absolute line/draw (same as Type 1 `lineto` operator)            |
| `ct`      | absolute curve (same as Type 1 `curveto` operator)               |
| `cp`      | end of sub-path (same as Type 1 `closepath` operator)            |
| `ed`      | end of glyph outline (same as Type 1 `endchar` operator)         |
| `preflx1` | marks start of flex operator, expressed as Type 1 path operators |
| `preflx2` | marks end of flex operator                                       |
| `flx`     | flex operator                                                    |
| `beginsubr snc` | marks start of new block of hint values                    |
| `endsubr enc\nnewcolors` | marks end of hint values                          |
| `rb`      | horizontal stem hint                                             |
| `ry`      | vertical stem hint                                               |
| `rm`      | vertical counter hints                                           |
| `rv`      | horizontal counter hints                                         |


Notes
-----
The `preflx1`/`preflx2` sequence provides the same info as the `flx` sequence;
the difference is that the `preflx1`/`preflx2` sequence provides the argument
values needed for building a Type 1 string while the `flx` sequence is simply
the 6 `rcurveto` points. Both sequences are always provided.
