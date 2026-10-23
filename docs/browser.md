# TV browser (EXPLORATORY, native Linux/ISO build)

Press **B** to open the browser page (drawn by the original firmware ZUI like the keyboard and the network page, see docs/osk.md).

| Part | File | Notes |
| --- | --- | --- |
| page UI | native/runtime/page_browser.cpp | address bar (OK opens the keyboard), Back, Close, 16 text lines with a highlight cursor, numbered links, status line |
| HTTP/HTTPS | native/runtime/http.cpp | HTTP/1.1 GET, redirects (up to 6), chunked bodies, `Accept-Encoding: identity`; HTTPS through mbedTLS 3.6 (TLS 1.2/1.3), certificate chain and host name verified against Mozilla's roots (`/etc/ssl/cacert.pem`, from curl.se/ca) |
| HTML to text | native/runtime/html_text.cpp | ASCII output (accents stripped, typographic quotes/dashes mapped), entities, `<pre>`, lists, links as `[n]`, word wrap with an estimate of the font's glyph widths |
| clock | native/iso/netup.c | NTP after DHCP (certificate validity needs the date) |

Keys: arrows move the cursor line, LEFT/RIGHT choose between links on one line, OK follows the selected link, PageUp/PageDown scroll, Backspace = Back, Esc closes.

Not there yet: forms (search boxes), cookies, POST, `<meta refresh>`, gzip, images, JavaScript. Sites that need JavaScript (including Google's search results) cannot work without those.
Mozilla's root bundle and mbedTLS are build inputs downloaded to `work/dl/tls` (not committed).
