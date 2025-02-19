# Contributing

When contributing to this repository, please do not feel intimidated! We welcome PRs from developers of all levels of experience and we were all new once.

## Pull Request Process

1. Pull requests should be made against the `dev` branch.
2. Ensure that the changed library can be built on your target system. Do not introduce any platform-
   specific code.
3. Ensure that all methods and functions you add are **fully documented** using doxygen style comments.
4. Test your commit! Make a simple single-file test bot to demonstrate the change, include this with the PR 
   as an attached file on a comment, so we can test and see how it works.
5. Ensure that you do not break any existing API calls without discussing on Discord first!
6. Be sure to follow the coding style guide (if you are not sure, match the code style of existing files
   including indent style etc).
7. Your PR must pass the CI actions before being allowed to be merged. Our PR actions check that the
   build will compile on various platforms before release and makes precompiled versions of the library.
8. If you are on the discord server for the project and your PR is accepted, let a moderator know and we
   will grant you the 'Contributors' role.
