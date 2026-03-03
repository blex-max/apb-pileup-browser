# Dev Log

## 03-03-26
(started devlog)
Moved from trying to pass around a global context instance
with subobjects describing modal contexts (e.g. pileup browser context)
to independent singleton context objects which can be retrieved by
type as needed. This was mostly inspired by realising that
getting a particular modal context subobject for a given app state (mode)
was a bit of a pain and would require std::variant or similar,
at least as far as I could see. The transition to the singleton pattern
was pleasantly easy and though it's not battle-tested at this point
it feels good so far.
