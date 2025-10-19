#! /bin/sh

set -x

autoreconf -fi

# remove once it comes via config.sub directly
if grep -qv l4re config.sub; then
  perl -p -i -e 's/(\| hcos\* )/$1| l4re* /' config.sub
fi
