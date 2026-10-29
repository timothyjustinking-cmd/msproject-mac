#!/usr/bin/env python3
"""The one neutral identity every oracle save carries.

MS Project writes the saving Windows account into the file: Application.UserName
becomes the document's LastAuthor, and the project's Author, Manager, Company and
Title travel with it.  A fixture that is committed, or that a later measurement
diffs, must carry none of the operator's name, company or local path, so every
oracle script calls neutralise() on the open project before it saves.

The body used to be copy-pasted into about twenty-five oracle scripts; it now
lives here.  "Fixture Builder" and "Fixture Company" are the wave-24 fixture
identities, and no fixture names a person.  A builder may still set
Application.UserName to AUTHOR before it opens a copy, because the project is
not available yet; neutralise() then sets it again with the project fields.

neutralise_active() is the form for a save helper that has the Application but
not the Project; FileSaveAs always writes the active project.

The null-save probes (oracle_w29_null_saves.py and oracle_w30_null_saves.py) are
the deliberate exception: they measure whether an unchanged save preserves the
fixture's own metadata, so they set only Application.UserName and must not
rewrite Author, Manager, Company or Title.
"""

AUTHOR = "Fixture Builder"
COMPANY = "Fixture Company"
DEFAULT_TITLE = "Synthetic Oracle Fixture"


def neutralise(app, project, title=None):
    """Give the application and the open project the neutral fixture identity.

    Call it before a save.  With no title the fixture gets DEFAULT_TITLE, so
    two saves of the same project (a control and its edited pair) carry the
    same Title.
    """
    app.UserName = AUTHOR
    project.Author = AUTHOR
    project.Manager = AUTHOR
    project.Company = COMPANY
    project.Title = DEFAULT_TITLE if title is None else title


def neutralise_active(app, title=None):
    """Neutralise the project that FileSaveAs will write."""
    neutralise(app, app.ActiveProject, title)
