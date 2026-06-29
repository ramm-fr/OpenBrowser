#!/bin/bash
echo "Opening OpenBrowser Landing Page at http://localhost:8080"
cd "$(dirname "$0")"
xdg-open http://localhost:8080 &
python3 -m http.server 8080
