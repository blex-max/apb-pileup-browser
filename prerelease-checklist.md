Before commmiting to main or making a release, please
check the following:

- The text body of manual (manual.cpp) and README.md must be in sync with the
  helptext (cli.cpp) and the implementation of commands (cmd.cpp).
- The helptext (cli.cpp) must be in sync with the CLI implementation;
  The helptext is not derived from the CLI implementation,
  it is manually maintained.
- The fixed in-app help messages (text_blocks.cpp) must be in sync with
  the navigation (event.cpp) and the cli (cli.cpp).
