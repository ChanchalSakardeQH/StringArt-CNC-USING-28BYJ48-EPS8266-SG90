# Third-party notices

ESP8266 String Art CNC is free software under the GNU General Public License,
version 3 or later — see [LICENSE](LICENSE). It includes work by other authors,
listed here under their own licences, which continue to apply to their parts.
Each licence below is compatible with the GPL, and each requires its notice to
be kept, which is what this file does.

---

## Cropper.js

- **Author:** Chen Fengyuan
- **Version:** 1.6.1
- **Source:** https://github.com/fengyuanchen/cropperjs
- **Licence:** MIT
- **Used in:** `web_page.h` — bundled into the web page, unmodified, so the
  photo cropper works without an internet connection. Its original copyright
  banner is kept in the file.

```
The MIT License (MIT)

Copyright 2015-present Chen Fengyuan

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## 5×7 ASCII font ("glcdfont")

- **Author:** Adafruit Industries, as distributed in Adafruit-GFX-Library.
  The glyph table itself descends from older LCD libraries.
- **Source:** https://github.com/adafruit/Adafruit-GFX-Library
- **Licence:** BSD 2-Clause
- **Used in:** `oled_display.h` — the glyph table only. The SSD1306 driver
  around it was written for this project and is GPL-licensed.

```
Software License Agreement (BSD License)

Copyright (c) 2012 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

---

## StringArt-CircleBase-Design

- **Author:** Chanchal Sakarde
- **Used in:** `web_page.h` — the base template designer (section 4 of the web
  page) is adapted from this design.
- **Licence:** not stated in the copy this project was built from.

This entry is Chanchal Sakarde own work, and you can relicense
it under the GPL along with the rest of the project. If you are not, confirm
the author's permission before publishing — adapting someone's work into a GPL
project needs either a compatible licence from them or their agreement. The
credit stays either way.
