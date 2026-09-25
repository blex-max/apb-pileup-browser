Before commmiting to main or making a release, please
check the following:

- The text body of manual (manual.cpp) and README.md must be in sync with the
  helptext (main.cpp).
- The helptext (main.cpp) must be in sync with the CLI implementation;
  The helptext is not derived from the CLI implementation,
  it is manually maintained.
- The fixed in-app help messages (helpblocks.cpp) must be in sync with
  the navigation (event.cpp), cli (main.cpp) and manual (manual.cpp).
- README.md is updated, including the readme roadmap.
