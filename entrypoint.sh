#!/bin/sh
# FlockSquawk Docker entrypoint
# Seeds pre-baked doctest.h into the bind-mounted source tree if missing,
# then hands off to the requested command.

DOCTEST_SRC="/opt/flocksquawk-deps/doctest.h"
DOCTEST_DST="test/doctest.h"

if [ -d "test" ] && [ ! -f "${DOCTEST_DST}" ] && [ -f "${DOCTEST_SRC}" ]; then
    cp "${DOCTEST_SRC}" "${DOCTEST_DST}"
fi

exec "$@"
