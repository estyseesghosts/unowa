always read AGENTS.md in entirety at the start of your work and after a compaction 

some of its most useful contents are mirrored here for you 

## NEVER WORK WITHOUT LOTUS 

IF YOU CANNOT DRIVE LOTUS OVER MCP, YOU STOP AND TELL THE USER 

YOU DO NOT START INVENTING BULLSHIT SOLUTIONS, YOU DO NOT START DRIVING IT WITH SCRIPTS, YOU STOP IMMEDIATELY.

## READ THE WIKI 

You're not expected to have deep technical knowledge of the 3DS platform or how to program efficiently for it. This information has been collected for you already. If it isn't already on disk, clone https://github.com/estyseesghosts/coffeepot.git. Always refer to coffeepot's CTR section when you're writing code or planning project changes - read the relevant hardware, firrmware or engineering articles, so you understand what you're working with. If you can't justify a change with real-hardware evidence or a citation from the wiki, you probably should not do it. Never edit coffeepot yourself. If you notice something genuinely incorrect, PR and tell the user. Always follow the wiki's C style guide.

## EIDOS

Try not to port solutions directly from Eidos, unless you're sure that's the best or only way to solve a certain problem. Review Eidos to understand how the gen 5 engine works. But our code needs to be much more tightly optimized around much weaker hardware. 

## Truth and Citations 

You are encouraged to come up with creative solutions for the obscure and arcane problems you might face while working in this folder. This does not mean you just throw shit at the wall to see what sticks. Every decision you make, every change you write, you must be able to justify that decision against a citation - either a line from a source file, the project documentation provided to you, or otherwise. Do not pull shit out of your ass.

## Half Steps 

Having a task's deliverables fully achieved on paper does not always mean those deliverables were completed correctly. Something like, being asked to complete a port, and giving me a result that is technically a complete port - that brute forces rendering in software, and as such runs terribly on real hardware. That's awful, benefits nobody, and wastes the users time and money. Do not find the minimum possible way to complete a deliverable if it means compromising on that deliverable, or giving the user something similar to -   but not exactly what they asked for.

## Git identity

The only git identity an agent may use in this repository is:

```text
terezi <jules@foxtails.me>
```

Set it in the repository itself before the first commit, and do not rely on a
machine-wide default or invent a placeholder:

```sh
git config user.name terezi
git config user.email jules@foxtails.me
```
