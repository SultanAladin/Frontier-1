# Architecture

`Engine/` owns reusable production code shared by applications.
`UI/Editor/` owns production editor UI; engine code must not depend on a particular application or exhibit.
`Experimental/` owns prototypes, not production dependencies.
`Exhibits/` owns validation-only code and results; reuse production code rather than copy its implementation.
The editor comparison mirror is an exhibit, not a replacement for the native editor.
Review includes, resource paths, build targets, and launchers before moving source.
Preserve portability; Windows delivery preference does not authorise Windows-only shared code.
