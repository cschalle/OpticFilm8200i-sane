This is the directory for release note fragments processed by
[towncrier](https://github.com/twisted/towncrier).

When making a user-visible change create a file in this directory and it will be automatically be
included into the release note document when the next release is published.

The file extension specifies the type of a change. The following are currently supported:

 - .backend: a new feature or a bugfix for a backend.
 - .frontend: a new feature or a bugfix for a frontend.
 - .security: a fix for security issue.
 - .removal: a deprecation or removal of functionality.
 - .misc: miscellaneous changes

Please don't add links to the merge requests into the release notes. If it's not clear where
the feature is coming from by looking into the git history (e.g. if a release note is being
made after the MR has landed) then add link to the MR to the commit description.

Please add links to fixed issues in Gitlab by using full URLs to them instead of Gitlab
shorthand syntax.
