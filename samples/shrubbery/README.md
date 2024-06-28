# `shrubbery`

**This sample assumes that you know a little bit of how Trieste
works! See the [infix](../infix) sample to learn more of the
basics.**

This sample implements a parser for (a subset of) [shrubbery
notation](https://plt.cs.northwestern.edu/pkg-build/doc/shrubbery/index.html).
Shrubbery notation is not a language per se, but a set of
syntactic conventions that group input into a predictable
structure (think S-expressions without all the parentheses). The
rest of this document gives an overview of shrubbery notation and
explains how it is implemented in Trieste. For the full details,
see the link above.

There are a few constructs from shrubbery notation that are not
supported by this implementation yet:

* Using single quotes as opener-closer pairs
* Line and column insensitivity with `«` `»`
* Block comments with `#//` `//#`
* `@`-notation
* Keywords prefixed by `~`

Some further simplifications have also been made:

* Lines cannot be continued by ending them with a backslash
* Strings do not support escaped characters
* The only numbers that can be parsed are integers

## Overview of Shrubbery Notation

Shrubbery notation is parsed into *groups*. A group is a sequence
of *terms* optionally ending with a *block* and an optional
sequence of *alternatives*. Terms include *atoms*, such as
identifiers, strings and numbers, *operators* such as `+` and
`>>=`, and *opener-closer pairs* including parentheses, brackets
and braces. The grammar can be sketched as follows:

    Group ::= Term* Block? (Alt+)?    (cannot be empty)
    Term  ::= Atom | Op | '(' Group ')' | '[' Group ']' | '{' Group '}'
    Block ::= ':' Group+
    Alt   ::= '|' Group+
    Op    ::= ...
    Atom  ::= ...

Most of these constructs are parsed as expected. For example,

    fun bump(x :: int): x + 1

is parsed into the following tree (shown using S-expressions, with
atoms printed as is):

    (Group fun bump (Paren (Group x (Op ::) int)) (Block (Group x (Op +) 1)))

However, shrubbery notation is also indentation sensitive,
following the general rule that lines that start on the same
indentation level are sibling groups. For example, the following
two programs

    here are two groups: number one
                         number two

    here are two groups:
        number one
        number two

are both parsed into:

    (Group here are two groups
           (Block (Group number one)
                  (Group number two)))

There are two exceptions to the general indentation rule. The
first is alternatives: an alternative that is on the same
indentation level as a previous group is added to that group:

    match n with
    | 0 => "zero"
    | _ => "more"

In the parsed representation, alternatives are merged into a
single `Alt` node containing a sequence of `Block` nodes, so the
parsed tree of the program above is:

    (Group match n with
           (Alt
             (Block (Group 0 (Op =>) "zero"))
             (Block (Group _ (Op =>) "more"))))

The other exception to the indentation rule is that lines that
begin with an operator (`+`, `-`, etc.) and is further indented
than the previous will continue that line:

    123 + 456
      + 789

A line can be continued like this several times, but subsequent
continuations must match the indentation level of the first
continuation:

    123 + 456
      + 789
      - abc

Groups on the same line can be separated by semi-colons, with one
exception: groups immediately under opener-closer pairs are
instead separated by commas. For example,

    int swap(int *x, int *y):
      var tmp = *x; *x = *y; *y = tmp;

is parsed as

    (Group int swap (Paren (Group int (Op *) x) (Group int (Op *) y))
           (Block (Group var tmp (Op =) (Op *) x)
                  (Group (Op *) x (Op =) (Op *) y)
                  (Group (Op *) y (Op =) tmp)))

(note how the empty group after the last semi-colon is discarded)

The whole parsed program is put inside a `Top` node containing the
top-level groups. For example, the following program

    fun fib 0: 0
      | fib 1: 1
      | fib n:
          val sum = fib (n-1) + fib (n-2)
          n + sum

    fun main():
      fib(5)

is parsed into the following tree (remember that alternatives are
merged into a sequence of blocks):

    (Top
      (Group fun fib 0 (Block (Group 0))
        (Alt
          (Block (Group fib 1 (Block (Group 1))))
          (Block
            (Group fib n
              (Block
                (Group val sum (Op =) fib (Paren (Group n (Op -) 1)) (Op +) fib (Paren (Group n (Op -) 2)))
                (Group n (Op +) sum))))))

      (Group fun main (Paren)
        (Block (Group fib (Paren (Group 5))))))


## Parsing Shubbery Notation

The Trieste implementation for parsing shrubbery notation starts
by grouping terms, including opener-closer pairs, according to the
indentation rules. Groups separated by commas or semi-colons are
aggregated under `Comma` and `Semi` nodes respectively. Blocks and
alternatives are identified, but alternatives will be added
*after* the group they belong to so as to keep the indentation
rule uniform for all constructs. A later rewrite pass moves them
into the right group.

Note how we don't need any special treatment of `Group`s as they
are already created automatically by Trieste.

There is no built-in support for handling indentation in Trieste,
so `parse.cc` defines some auxiliary machinery for remembering
previous indentation levels. New indentation levels are
established by the column of first term of the program or the
column of the first term in a block (after `:`), in an alternative
(after `|`), or after an opener (`(`, `[` or `{`). New indentation
levels must be strictly larger than previous levels.

The first term after a newline must match a previously established
indentation level and closes any open blocks or alternatives with
larger established indentation levels. This means that there is a
stack of indentation levels which is pushed to when establishing a
new level and popped from when the scope of an established
indentation level closes, either due to a closer (`)`, `]` or `}`)
or due to matching a smaller indentation level. The first term
after a newline establishes a group that becomes a sibling to the
previous group at the same indentation level.

The well-formedness specification for the parsing pass says very
little about the structure since commas and semi-colons can appear
virtually anywhere:

    inline const auto wf_term = Paren | Bracket | Brace |
                                Block | Alt | Op | Atom;

    inline const auto wf_grouping_construct = Comma | Semi | Group;

    inline const auto wf_parser =
        (Top <<= File)
      | (File    <<= wf_grouping_construct++)
      | (Paren   <<= wf_grouping_construct++)
      | (Bracket <<= wf_grouping_construct++)
      | (Brace   <<= wf_grouping_construct++)
      | (Block   <<= wf_grouping_construct++)
      | (Alt     <<= wf_grouping_construct++)
      | (Comma   <<= (Semi | Group)++)
      | (Semi    <<= (Comma | Group)++)
      | (Group   <<= wf_term++)
      ;

The first rewriting pass (see `reader.cc`) ensures that commas and semi-colons
appear in the right places and that blocks and alternatives are not empty
(except in a few special cases). It also throws away empty groups caused by
semi-colons. The resulting well-formedness specification now says where commas
and semi-colons may appear and that alternatives and comma-separated sequences
cannot be empty:

    inline const auto wf_check_parser =
        wf_parser
      | (File    <<= (Group | Semi)++)
      | (Paren   <<= (Group | Comma)++)
      | (Bracket <<= (Group | Comma)++)
      | (Brace   <<= (Group | Comma)++)
      | (Block   <<= (Group | Semi)++)
      | (Alt     <<= (Group | Semi)++[1])
      | (Comma   <<= Group++[1])
      | (Semi    <<= Group++)
      ;

The next pass finds alternatives and puts them last in the
preceding group (unless separated by a comma or semi-colon). It
also merges multiple alternatives into a single `Alt` node holding
a sequence of `Block` nodes. The only change to well-formedness is
that `Alt` nodes now hold a non-empty sequence of `Block`s:

    inline const auto wf_alternatives =
        wf_check_parser
      | (Alt <<= Block++[1])
      ;

The next pass removes the `Comma` and `Semi` blocks
altogether, replacing them by their children. This pass also
handles the restriction that alternatives can only appear first in
a group if nested immediately under a `Brace` or `Bracket` node.
The final well-formedness specification says that everything
contains a sequence of groups, except alternatives and groups
which retain their previous definitions:

    inline const auto wf_drop_separators =
        wf_alternatives
      | (File    <<= Group++)
      | (Paren   <<= Group++)
      | (Bracket <<= Group++)
      | (Brace   <<= Group++)
      | (Block   <<= Group++)
      ;

After removing the separators, we check the special rule that alternatives may
not not appear first in their group, except when directly under a brace or
bracket. This pass does not change the well-formedness specification.

Finally, we are ready to move to our final well-formedness specification that
says that a group is a sequence of terms followed by an optional `Block` node
and an optional (non-empty) `Alt` node. However, well-formedness specifications
currently do not support following a repetition (e.g. `wf_term++`) with another
token, nor do they support optional entries. The closest we can get is grouping
all the terms of a group under a node `Terms` and then emulate optional entries
by introducing a special token `None` that is used to mean that a `Block`/`Alt`
is not there. This indirection aside, we get a well-formedness specification
that corresponds to the original grammar of shrubbery notation:

    inline const auto wf =
        (Top <<= File)
      | (File <<= Group++)
      | (Paren <<= Group++)
      | (Bracket <<= Group++)
      | (Brace <<= Group++)
      | (Block <<= Group++)
      | (Alt <<= Block++[1])
      | (Group <<= Terms * (Block >>= Block | None) * (Alt >>= Alt | None))
      | (Terms <<= (Paren | Bracket | Brace | Op | Atom)++)
      ;

