The Standard Bezier format
============================

The Standard Bezier format is an plain text format consisting of the following
operators:

| Operator  | Description                                            |
|-----------|--------------------------------------------------------|
| `sc`      | start of character outline                             |
| `rmt`     | relative move (same as T1 rmoveto operator)            |
| `dt`      | absolute line/draw (same as T1 lineto operator)        |
| `ct`      | absolute curve (same as T1 curveto operator)           |
| `cp`      | end of sub-path (same as T1 closepath operator)        |
| `ed`      | end of character outline (same as T1 endchar operator) |
| `preflx1` | marks start of flex op, expressed as T1 path ops       |
| `preflx2` | marks end of flex op                                   |
| `flx`     | flex op                                                |
| `beginsubr snc` | marks start of new block of hint values          |
| `endsubr enc\nnewcolors` | marks end of hint values                |
| `rb`      | horizontal stem hint                                   |
| `ry`      | vertical stem hint                                     |
| `rm`      | vertical counter hints                                 |
| `rv`      | horizontal counter hints                               |


Notes
-----
The `preflx1`/`preflx2` sequence provides the same info as the `flx` sequence;
the difference is that the `preflx1`/`preflx2` sequence provides the argument
values needed for building a Type1 string while the `flx` sequence is simply
the 6 rcurveto points. Both sequences are always provided.
