We use ronn - a Ruby gem - to convert Markdown files into troff-formatted
manual pages. Both the source file and the resulting manual page are committed
to version control.

In order to convert the Markdown source code:

```sh
$ ronn --warnings mbeat_pub.md # this will generate mbeat_pub.8
$ ronn --warnings mbeat_sub.md # this will generate mbeat_sub.8
```
