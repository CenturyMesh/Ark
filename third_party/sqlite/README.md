# SQLite 3.54.0

Unmodified `sqlite3.c`, `sqlite3.h` and `sqlite3ext.h` from the official
amalgamation archive downloaded on 2026-10-10:
https://www.sqlite.org/2026/sqlite-amalgamation-3540000.zip

The archive was verified against the official download page SHA3-256:
`7b670a62fdfbd672b75fef004cb703c8a3e87d3a5cc7d675b4a08337004a2d93`.

SQLite is public domain: https://www.sqlite.org/copyright.html
Its source license notice is retained. ARK builds the C engine as a separate
static target with FTS5, thread safety, foreign keys enabled by default,
double-quoted string literals disabled, and runtime extension loading disabled.
No database server, extra application runtime or system SQLite DLL is required.
