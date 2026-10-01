# Web Flasher Hosting

This folder is ready to be hosted as a small HTTPS website.

ESP Web Tools requires HTTPS (or localhost) because Web Serial is a secure-context browser feature.

## GitHub Pages

A simple approach:

1. Put the contents of this `web-flasher` folder in a GitHub repository, for example under `/docs`.
2. In GitHub repository Settings → Pages, publish that folder/branch.
3. Open the resulting `https://...github.io/...` URL in Chrome or Edge.
4. Click **Connect & Install**.

The manifest uses a single merged ESP32 image built from the exact tested Stage 5.10 binaries.

Factory web installation is intended for new/recovery boards and can erase internal VMU/pairing data.

Reference:
https://github.com/esphome/esp-web-tools
