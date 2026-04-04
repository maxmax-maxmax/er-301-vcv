#!/bin/bash
exec /opt/homebrew/bin/python3 -m http.server ${PORT:-8090} --directory .
