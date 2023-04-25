# logrotate contributions

## Pull requests

  - Fork it.
  - Create your feature branch (`git checkout -b fixing-blah`), please avoid working directly on the `master` branch.
  - Run tests with `make check`. Add or modify tests if needed.
  - Check for unnecessary whitespaces with `git diff --check` before committing.
  - [optional] Add user visible changes to `ChangeLog.md` under the `UNRELEASED` section
  - Commit your changes, try to follow this format:
```
scope: short summary of the change
[empty line]
Long description of the change, explanation of why the change is useful, etc.
```
  - Push to the branch (`git push -u origin fixing-blah`).
  - Create a new pull request.
  - Check that the pull request does not cause [Travis](https://travis-ci.org/logrotate/logrotate) to fail.
