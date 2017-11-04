We use ronn - a Ruby gem - to convert Markdown files into troff-formatted
manual pages. Both the source file and the resulting manual page are committed
to version control.

In order to convert the Markdown source code:

```sh
$ ronn --warnings mpub.md  # this will generate mpub.8
$ ronn --warnings msub.md  # this will generate msub.8
```
