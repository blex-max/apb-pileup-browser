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

## 05-03-26
singleton pattern continues to feel nice. I am focusing on trying to set
up as much as possible before even touching htslib, i.e. real data. Today
I've implemented basic drawing functions for the main pileup browser mode.
I'm pleased with the appearance so far. Next I want to introduce a debug
display which I'm sure will come in useful. Not much else to report really,
development today has felt gratifyingly smooth.

## 23-03-26
Implelemented slightly more featureful command input (backspace, really).
Implemented rendering of arbitrary debug info to display, which involved
road-testing the commands implementation a bit. All went reasonably well.
I think it is now time to move on to displaying pileup data.
Also did various restructing/reorg.

## 24-03-26
I realised that before displaying real data, there's further to go
with fake data! Figured out a modular and
configurable table display system yet, for displaying properties
of the pileup base. It is perhaps imperfect but quite functional!
I've also implemented basic user commands for showing/hiding
pileup/read properties at request. Still, before moving on
to real data I need to work out what happens when there is more
data than can be shown on screen at once.

# 25-04-26
I started working on the data model and plugging it in to the UI.
In the future I want to consider the ability to dump to TSV, so
decoupling is important.
